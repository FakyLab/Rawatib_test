#include "utils/ImportHelper.h"
#include "utils/AuditLog.h"
#include "ui/dialogs/ImportPreviewDialog.h"
#include "repositories/AttendanceRepository.h"
#include "repositories/EmployeeRepository.h"
#include "database/DatabaseManager.h"
#include "models/AttendanceRecord.h"
#include "models/Employee.h"
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QDate>
#include <QTime>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QSqlError>
#include <cmath>
#include <algorithm>

static inline QString tr(const char* key) {
    return QCoreApplication::translate("ImportHelper", key);
}

namespace ImportHelper {

// ═══════════════════════════════════════════════════════════════════════════
// ParsedEmployee convenience counts
// ═══════════════════════════════════════════════════════════════════════════

int ParsedEmployee::cleanCount() const {
    int n = 0;
    for (const auto& r : records)
        if (r.status == ParsedRecord::Status::Clean) ++n;
    return n;
}
int ParsedEmployee::softConflictCount() const {
    int n = 0;
    for (const auto& r : records)
        if (r.status == ParsedRecord::Status::SoftConflict) ++n;
    return n;
}
int ParsedEmployee::hardErrorCount() const {
    int n = 0;
    for (const auto& r : records)
        if (r.status == ParsedRecord::Status::HardError) ++n;
    return n;
}
int ParsedEmployee::selectedCount() const {
    int n = 0;
    for (const auto& r : records)
        if (r.selected) ++n;
    return n;
}

// ═══════════════════════════════════════════════════════════════════════════
// ParsePass1Result convenience count
// ═══════════════════════════════════════════════════════════════════════════

int ParsePass1Result::totalSelectedCount() const {
    int n = 0;
    for (const auto& emp : employees)
        n += emp.selectedCount();
    return n;
}

// ═══════════════════════════════════════════════════════════════════════════
// CSV helpers
// ═══════════════════════════════════════════════════════════════════════════

static QStringList parseCsvLine(const QString& line) {
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.length(); ++i) {
        const QChar c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line[i+1] == '"') {
                current += '"'; ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.append(current.trimmed());
            current.clear();
        } else {
            current += c;
        }
    }
    fields.append(current.trimmed());
    return fields;
}

static bool isHeaderRow(const QString& col0) {
    return col0.compare("Date", Qt::CaseInsensitive) == 0 ||
           col0 == QString::fromUtf8("\xd8\xa7\xd9\x84\xd8\xaa\xd8\xa7\xd8\xb1\xd9\x8a\xd8\xae"); // التاريخ
}
static bool isDataRow(const QString& col0) {
    return QDate::fromString(col0, "yyyy-MM-dd").isValid();
}
static bool isSummaryMarker(const QString& col0) {
    return col0.compare("Summary", Qt::CaseInsensitive) == 0 ||
           col0 == QString::fromUtf8("\xd8\xa7\xd9\x84\xd9\x85\xd9\x84\xd8\xae\xd8\xb5") || // الملخص
           col0.compare("TOTAL", Qt::CaseInsensitive) == 0;
}
static bool isPayrollMarker(const QString& col0) {
    return col0.compare("Payroll Adjustments", Qt::CaseInsensitive) == 0 ||
           col0 == QString::fromUtf8("\xd8\xaa\xd8\xb9\xd8\xaf\xd9\x8a\xd9\x84\xd8\xa7\xd8\xaa \xd8\xa7\xd9\x84\xd8\xb1\xd8\xa7\xd8\xaa\xd8\xa8"); // تعديلات الراتب
}
static bool parsePaidStatus(const QString& s) {
    return s.compare("Paid", Qt::CaseInsensitive) == 0 ||
           s == QString::fromUtf8("\xd9\x85\xd8\xaf\xd9\x81\xd9\x88\xd8\xb9"); // مدفوع
}

// ── Employee lookup ────────────────────────────────────────────────────────

static std::optional<Employee> findEmployee(const QString& name,
                                             const QVector<Employee>& employees) {
    for (const auto& e : employees)
        if (e.name.compare(name, Qt::CaseInsensitive) == 0)
            return e;
    return std::nullopt;
}

static QString uniqueName(const QString& baseName,
                           const QVector<Employee>& existing) {
    QStringList names;
    for (const auto& e : existing) names << e.name.toLower();
    if (!names.contains(baseName.toLower())) return baseName;
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = QString("%1 (%2)").arg(baseName).arg(n);
        if (!names.contains(candidate.toLower())) return candidate;
    }
    return baseName + " (new)";
}

// ── Intra-file overlap check ───────────────────────────────────────────────

static QString checkIntraFileOverlap(const QVector<ParsedRecord>& existing,
                                      const QDate& date,
                                      const QTime& newIn,
                                      const QTime& newOut,
                                      bool newIsOpen)
{
    for (const auto& r : existing) {
        if (r.date != date) continue;
        if (r.status == ParsedRecord::Status::HardError) continue;
        const QTime existIn  = r.checkIn;
        const QTime existOut = r.checkOut;
        const bool  existOpen = r.isOpen;

        if (existOpen) {
            if (newIn >= existIn)
                return tr("Overlaps another record in this file (open session from %1).")
                    .arg(existIn.toString("hh:mm AP"));
            continue;
        }
        if (newIsOpen) {
            if (newIn < existOut && existIn < newIn)
                return tr("Open record starts inside another record in this file (%1\xe2\x80\x93%2).")
                    .arg(existIn.toString("hh:mm AP"), existOut.toString("hh:mm AP"));
            if (existIn >= newIn)
                return tr("Open record conflicts with another record in this file (%1\xe2\x80\x93%2).")
                    .arg(existIn.toString("hh:mm AP"), existOut.toString("hh:mm AP"));
            continue;
        }
        if (newIn < existOut && existIn < newOut)
            return tr("Overlaps another record in this file (%1\xe2\x80\x93%2).")
                .arg(existIn.toString("hh:mm AP"), existOut.toString("hh:mm AP"));
    }
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════
// PASS 1 — parseFile()
// ═══════════════════════════════════════════════════════════════════════════

ParsePass1Result parseFile(const QString& filePath)
{
    ParsePass1Result result;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.fileError = tr("Could not open file: %1").arg(filePath);
        return result;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    qDebug() << "[parseFile] starting:" << filePath;
    const QVector<Employee> allEmployees =
        EmployeeRepository::instance().getAllEmployees();
    qDebug() << "[parseFile] loaded" << allEmployees.size() << "employees from DB";

    // Use an index rather than a raw pointer — QVector can reallocate on append,
    // which would leave a raw pointer dangling.
    int  currentEmpIndex = -1;
    bool inDataBlock    = false;
    bool inSummaryBlock = false;
    bool inPayrollBlock = false;
    int  lineNumber     = 0;

    // Safe accessor: returns nullptr if no current employee.
    auto currentEmp = [&]() -> ParsedEmployee* {
        if (currentEmpIndex < 0 || currentEmpIndex >= (int)result.employees.size())
            return nullptr;
        return &result.employees[currentEmpIndex];
    };

    while (!in.atEnd()) {
        const QString rawLine = in.readLine();
        ++lineNumber;
        QString line = rawLine;
        if (lineNumber == 1 && line.startsWith("\xEF\xBB\xBF"))
            line = line.mid(3);

        const QStringList cols = parseCsvLine(line);
        const QString col0 = cols.isEmpty() ? QString() : cols[0];

        // Blank line — reset block flags
        if (line.trimmed().isEmpty()) {
            inDataBlock = inSummaryBlock = inPayrollBlock = false;
            continue;
        }

        // Payroll block — skip entirely
        if (isPayrollMarker(col0)) {
            inPayrollBlock = true; inDataBlock = inSummaryBlock = false;
            continue;
        }
        if (inPayrollBlock) continue;

        // ── Employee name line ─────────────────────────────────────────────
        const QString arabicEmployee = QString::fromUtf8("\xd8\xa7\xd9\x84\xd9\x85\xd9\x88\xd8\xb8\xd9\x81"); // الموظف
        if ((col0.compare("Employee", Qt::CaseInsensitive) == 0 || col0 == arabicEmployee)
            && cols.size() >= 2)
        {
            result.employees.append(ParsedEmployee{});
            currentEmpIndex = (int)result.employees.size() - 1;
            currentEmp()->csvName = cols[1].trimmed();
            qDebug() << "[parseFile] employee block:" << currentEmp()->csvName;
            inDataBlock = inSummaryBlock = inPayrollBlock = false;

            auto found = findEmployee(currentEmp()->csvName, allEmployees);
            // Always generate suggestedNewName — needed if admin later picks
            // "Create as new employee" even for an existing employee (wage mismatch).
            currentEmp()->suggestedNewName =
                uniqueName(currentEmp()->csvName, allEmployees);
            if (found) {
                currentEmp()->existingEmployee = found;
                currentEmp()->resolution = ParsedEmployee::Resolution::UseExisting;
            } else {
                currentEmp()->resolution = ParsedEmployee::Resolution::CreateNew;
            }
            continue;
        }

        // ── Period line ────────────────────────────────────────────────────
        const QString arabicPeriod = QString::fromUtf8("\xd8\xa7\xd9\x84\xd9\x81\xd8\xaa\xd8\xb1\xd8\xa9"); // الفترة
        if ((col0.compare("Period", Qt::CaseInsensitive) == 0 || col0 == arabicPeriod)
            && cols.size() >= 2 && currentEmp())
        {
            currentEmp()->period = cols[1].trimmed();
            continue;
        }

        // ── Hourly Wage Raw (machine-readable, preferred) ──────────────────
        if (col0 == "Hourly Wage Raw" && cols.size() >= 2 && currentEmp()) {
            bool ok = false;
            const double w = cols[1].trimmed().toDouble(&ok);
            if (ok && w >= 0.0) {
                currentEmp()->csvWage     = w;
                currentEmp()->wageParseOk = true;
                currentEmp()->csvPayType  = PayType::Hourly;
                if (currentEmp()->existingEmployee) {
                    const double dbWage = currentEmp()->existingEmployee->hourlyWage;
                    currentEmp()->resolution = std::fabs(dbWage - w) < 0.001
                        ? ParsedEmployee::Resolution::UseExisting
                        : ParsedEmployee::Resolution::UseExistingWarn;
                }
            }
            continue;
        }

        // ── Hourly Wage formatted (fallback for older exports) ─────────────
        const QString arabicWage = QString::fromUtf8("\xd8\xa7\xd9\x84\xd8\xa3\xd8\xac\xd8\xb1 \xd8\xa7\xd9\x84\xd8\xb3\xd8\xa7\xd8\xb9\xd9\x8a"); // الأجر الساعي
        if ((col0.compare("Hourly Wage", Qt::CaseInsensitive) == 0 || col0 == arabicWage)
            && cols.size() >= 2 && currentEmp() && !currentEmp()->wageParseOk)
        {
            QString digits;
            for (const QChar c : cols[1].trimmed())
                if (c.isDigit() || c == '.' || c == '-') digits += c;
            bool ok = false;
            const double w = digits.toDouble(&ok);
            if (ok && w >= 0.0) {
                currentEmp()->csvWage     = w;
                currentEmp()->wageParseOk = true;
                currentEmp()->csvPayType  = PayType::Hourly;
                if (currentEmp()->existingEmployee) {
                    const double dbWage = currentEmp()->existingEmployee->hourlyWage;
                    currentEmp()->resolution = std::fabs(dbWage - w) < 0.001
                        ? ParsedEmployee::Resolution::UseExisting
                        : ParsedEmployee::Resolution::UseExistingWarn;
                }
            }
            continue;
        }

        // ── Monthly Salary Raw (machine-readable, preferred) ───────────────
        if (col0 == "Monthly Salary Raw" && cols.size() >= 2 && currentEmp()) {
            bool ok = false;
            const double w = cols[1].trimmed().toDouble(&ok);
            if (ok && w >= 0.0) {
                currentEmp()->csvWage     = w;
                currentEmp()->wageParseOk = true;
                currentEmp()->csvPayType  = PayType::Monthly;
                if (currentEmp()->existingEmployee) {
                    // Compare against DB monthly salary, not hourlyWage
                    const double dbSalary = currentEmp()->existingEmployee->monthlySalary;
                    currentEmp()->resolution = std::fabs(dbSalary - w) < 0.001
                        ? ParsedEmployee::Resolution::UseExisting
                        : ParsedEmployee::Resolution::UseExistingWarn;
                }
            }
            continue;
        }

        // ── Monthly Salary formatted (fallback for wage-visible exports) ───
        if (col0.compare("Monthly Salary", Qt::CaseInsensitive) == 0
            && cols.size() >= 2 && currentEmp() && !currentEmp()->wageParseOk)
        {
            QString digits;
            for (const QChar c : cols[1].trimmed())
                if (c.isDigit() || c == '.' || c == '-') digits += c;
            bool ok = false;
            const double w = digits.toDouble(&ok);
            if (ok && w >= 0.0) {
                currentEmp()->csvWage     = w;
                currentEmp()->wageParseOk = true;
                currentEmp()->csvPayType  = PayType::Monthly;
                if (currentEmp()->existingEmployee) {
                    const double dbSalary = currentEmp()->existingEmployee->monthlySalary;
                    currentEmp()->resolution = std::fabs(dbSalary - w) < 0.001
                        ? ParsedEmployee::Resolution::UseExisting
                        : ParsedEmployee::Resolution::UseExistingWarn;
                }
            }
            continue;
        }

        // ── Working Days/Month ─────────────────────────────────────────────
        if (col0.compare("Working Days/Month", Qt::CaseInsensitive) == 0
            && cols.size() >= 2 && currentEmp())
        {
            bool ok = false;
            const int days = cols[1].trimmed().toInt(&ok);
            if (ok && days > 0)
                currentEmp()->csvWorkingDays = days;
            continue;
        }

        // ── Expected Check-In ──────────────────────────────────────────────
        if (col0.compare("Expected Check-In", Qt::CaseInsensitive) == 0
            && cols.size() >= 2 && currentEmp())
        {
            const QString val = cols[1].trimmed();
            if (!val.isEmpty())
                currentEmp()->csvExpectedCheckin =
                    QTime::fromString(val, "HH:mm");
            continue;
        }

        // ── Expected Check-Out ─────────────────────────────────────────────
        if (col0.compare("Expected Check-Out", Qt::CaseInsensitive) == 0
            && cols.size() >= 2 && currentEmp())
        {
            const QString val = cols[1].trimmed();
            if (!val.isEmpty())
                currentEmp()->csvExpectedCheckout =
                    QTime::fromString(val, "HH:mm");
            continue;
        }

        // ── Exported date line ─────────────────────────────────────────────
        const QString arabicExported = QString::fromUtf8("\xd8\xaa\xd8\xa7\xd8\xb1\xd9\x8a\xd8\xae \xd8\xa7\xd9\x84\xd8\xaa\xd8\xb5\xd8\xaf\xd9\x8a\xd8\xb1"); // تاريخ التصدير
        if ((col0.compare("Exported", Qt::CaseInsensitive) == 0 || col0 == arabicExported)
            && cols.size() >= 2 && currentEmp())
        {
            currentEmp()->exportedDate = cols[1].trimmed();
            continue;
        }

        // ── Column header row ──────────────────────────────────────────────
        if (isHeaderRow(col0)) {
            qDebug() << "[parseFile] data block start (line" << lineNumber << ")";
            inDataBlock = true; inSummaryBlock = false;
            continue;
        }

        // ── Summary block marker ───────────────────────────────────────────
        if (isSummaryMarker(col0)) {
            inDataBlock = false; inSummaryBlock = true;
            continue;
        }

        // ── Summary values — read for checksum ────────────────────────────
        if (inSummaryBlock && currentEmp() && cols.size() >= 2) {
            bool ok = false;
            const double v = cols[1].trimmed().toDouble(&ok);
            const QString arabicHours  = QString::fromUtf8("\xd8\xa5\xd8\xac\xd9\x85\xd8\xa7\xd9\x84\xd9\x8a \xd8\xa7\xd9\x84\xd8\xb3\xd8\xa7\xd8\xb9\xd8\xa7\xd8\xaa");
            const QString arabicSalary = QString::fromUtf8("\xd8\xa5\xd8\xac\xd9\x85\xd8\xa7\xd9\x84\xd9\x8a \xd8\xa7\xd9\x84\xd8\xb1\xd8\xa7\xd8\xaa\xd8\xa8");
            if (col0.compare("Total Hours", Qt::CaseInsensitive) == 0 || col0 == arabicHours)
                { if (ok) currentEmp()->csvSummaryTotalHours  = v; }
            else if (col0.compare("Total Salary", Qt::CaseInsensitive) == 0 || col0 == arabicSalary)
                { if (ok) currentEmp()->csvSummaryTotalSalary = v; }
            continue;
        }

        // ── Data row ───────────────────────────────────────────────────────
        if (inDataBlock && isDataRow(col0)) {
            if (!currentEmp()) continue;

            ParsedRecord rec;
            rec.csvRowNumber = lineNumber;

            if (cols.size() < 3) {
                rec.status = ParsedRecord::Status::HardError;
                rec.issueDescription = tr("Line %1: too few columns.").arg(lineNumber);
                rec.selected = false;
                currentEmp()->records.append(rec);
                continue;
            }

            // Date
            rec.date = QDate::fromString(col0, "yyyy-MM-dd");
            if (!rec.date.isValid()) {
                rec.status = ParsedRecord::Status::HardError;
                rec.issueDescription = tr("Line %1: invalid date \"%2\".").arg(lineNumber).arg(col0);
                rec.selected = false;
                currentEmp()->records.append(rec);
                continue;
            }

            // Check-In
            rec.checkIn = QTime::fromString(cols[1].trimmed(), "HH:mm");
            if (!rec.checkIn.isValid()) {
                rec.status = ParsedRecord::Status::HardError;
                rec.issueDescription = tr("Line %1: invalid check-in \"%2\".").arg(lineNumber).arg(cols[1]);
                rec.selected = false;
                currentEmp()->records.append(rec);
                continue;
            }

            // Check-Out
            const QString coStr = cols[2].trimmed();
            rec.isOpen = (coStr == "--" || coStr.isEmpty());
            if (!rec.isOpen) {
                rec.checkOut = QTime::fromString(coStr, "HH:mm");
                if (!rec.checkOut.isValid()) {
                    rec.status = ParsedRecord::Status::HardError;
                    rec.issueDescription = tr("Line %1: invalid check-out \"%2\".").arg(lineNumber).arg(coStr);
                    rec.selected = false;
                    currentEmp()->records.append(rec);
                    continue;
                }
                if (rec.checkOut <= rec.checkIn) {
                    rec.status = ParsedRecord::Status::HardError;
                    rec.issueDescription = tr("Line %1: check-out must be after check-in.").arg(lineNumber);
                    rec.selected = false;
                    currentEmp()->records.append(rec);
                    continue;
                }
            }

            // Hours worked (col 3)
            if (cols.size() > 3 && !rec.isOpen) {
                bool ok = false;
                rec.hoursWorked = cols[3].trimmed().toDouble(&ok);
                if (!ok) rec.hoursWorked = 0.0;
            }

            // Daily wage / Net Day (col 4)
            if (cols.size() > 4 && !rec.isOpen) {
                bool ok = false;
                rec.dailyWage = cols[4].trimmed().toDouble(&ok);
                if (!ok) rec.dailyWage = 0.0;
            }

            // Paid status (col 5)
            if (cols.size() > 5)
                rec.paid = parsePaidStatus(cols[5].trimmed());

            // Base Rate (col 6) — monthly only, silently 0.0 for hourly
            if (cols.size() > 6 && !rec.isOpen) {
                bool ok = false;
                const double v = cols[6].trimmed().toDouble(&ok);
                if (ok) rec.baseDailyRate = v;
            }

            // Day Deduction (col 7) — monthly only, silently 0.0 for hourly
            if (cols.size() > 7 && !rec.isOpen) {
                bool ok = false;
                const double v = cols[7].trimmed().toDouble(&ok);
                if (ok) rec.dayDeduction = v;
            }

            // Intra-file overlap check
            {
                const QString err = checkIntraFileOverlap(
                    currentEmp()->records, rec.date, rec.checkIn, rec.checkOut, rec.isOpen);
                if (!err.isEmpty()) {
                    rec.status = ParsedRecord::Status::SoftConflict;
                    rec.issueDescription = err;
                    rec.selected = true;
                    currentEmp()->records.append(rec);
                    continue;
                }
            }

            // DB overlap check (only for existing employees)
            if (currentEmp()->existingEmployee) {
                const int empId = currentEmp()->existingEmployee->id;
                const auto existing = AttendanceRepository::instance()
                    .getRecordsForMonth(empId, rec.date.year(), rec.date.month());

                for (const auto& ex : existing) {
                    if (ex.date != rec.date) continue;
                    const bool exOpen = ex.isOpen();
                    QString overlapErr;

                    if (exOpen) {
                        if (rec.checkIn >= ex.checkIn)
                            overlapErr = tr("Overlaps existing open session from %1.")
                                .arg(ex.checkIn.toString("hh:mm AP"));
                    } else if (rec.isOpen) {
                        if (rec.checkIn < ex.checkOut && ex.checkIn < rec.checkIn)
                            overlapErr = tr("Open record starts inside existing session (%1\xe2\x80\x93%2).")
                                .arg(ex.checkIn.toString("hh:mm AP"), ex.checkOut.toString("hh:mm AP"));
                        else if (ex.checkIn >= rec.checkIn)
                            overlapErr = tr("Open record conflicts with existing session (%1\xe2\x80\x93%2).")
                                .arg(ex.checkIn.toString("hh:mm AP"), ex.checkOut.toString("hh:mm AP"));
                    } else {
                        if (rec.checkIn < ex.checkOut && ex.checkIn < rec.checkOut)
                            overlapErr = tr("Overlaps existing session (%1\xe2\x80\x93%2).")
                                .arg(ex.checkIn.toString("hh:mm AP"), ex.checkOut.toString("hh:mm AP"));
                    }

                    if (!overlapErr.isEmpty()) {
                        rec.status = ParsedRecord::Status::SoftConflict;
                        rec.issueDescription = overlapErr;
                        rec.selected = true;
                        break;
                    }
                }
            }

            currentEmp()->records.append(rec);
            continue;
        }
        // All other lines silently skipped
    }
    file.close();

    // ── Post-parse: checksum + result flags ────────────────────────────────
    for (auto& emp : result.employees) {
        // Checksum validation
        if (emp.csvSummaryTotalSalary >= 0.0) {
            double parsedTotal = 0.0;
            for (const auto& r : emp.records)
                if (r.status != ParsedRecord::Status::HardError && !r.isOpen)
                    parsedTotal += r.dailyWage;
            if (std::fabs(parsedTotal - emp.csvSummaryTotalSalary) > 0.02) {
                emp.checksumMismatch = true;
                emp.checksumNote = tr(
                    "Salary total in file (%1) differs from sum of parsed records (%2). "
                    "File may be incomplete or manually edited.")
                    .arg(emp.csvSummaryTotalSalary, 0, 'f', 2)
                    .arg(parsedTotal, 0, 'f', 2);
            }
        }

        // CreateNew with no parseable wage — previously a hard error,
        // now allowed: admin can enter a manual wage in ImportPreviewDialog,
        // or the employee is created with wage 0 and edited after import.
        // Records are left as Clean/SoftConflict so they can be selected.
        if (emp.resolution == ParsedEmployee::Resolution::CreateNew && !emp.wageParseOk) {
            for (auto& r : emp.records) {
                if (r.status == ParsedRecord::Status::HardError)
                    r.status = ParsedRecord::Status::SoftConflict;
                r.issueDescription = tr(
                    "Wage not in file — will use entered wage or 0.");
            }
        }

        // Update result-level flags
        for (const auto& r : emp.records) {
            if (r.status != ParsedRecord::Status::HardError)
                result.hasAnyImportable = true;
        }

        if (emp.resolution == ParsedEmployee::Resolution::UseExistingWarn ||
            emp.resolution == ParsedEmployee::Resolution::CreateNew        ||
            emp.softConflictCount() > 0 || emp.hardErrorCount() > 0       ||
            emp.checksumMismatch)
        {
            result.hasAnyIssues = true;
        }
    }

    qDebug() << "[parseFile] done. employees:" << result.employees.size()
             << "hasAnyIssues:" << result.hasAnyIssues
             << "hasAnyImportable:" << result.hasAnyImportable;
    for (const auto& e : result.employees)
        qDebug() << "[parseFile]  " << e.csvName
                 << "records:" << e.records.size()
                 << "resolution:" << (int)e.resolution
                 << "wageParseOk:" << e.wageParseOk;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// PASS 2 — commitImport()
// ═══════════════════════════════════════════════════════════════════════════

ImportResult commitImport(ParsePass1Result& pass1)
{
    qDebug() << "[Import] commitImport starting. employees:" << pass1.employees.size();
    ImportResult result;
    auto& db = DatabaseManager::instance().database();
    qDebug() << "[Import] db.isOpen:" << db.isOpen();
    if (!db.transaction()) {
        qDebug() << "[Import] db.transaction() FAILED:" << db.lastError().text();
    } else {
        qDebug() << "[Import] db.transaction() OK";
    }

    for (auto& emp : pass1.employees) {
        qDebug() << "[Import] processing employee:" << emp.csvName
                 << "resolution:" << (int)emp.resolution
                 << "existingEmployee set:" << emp.existingEmployee.has_value()
                 << "selected records:" << emp.selectedCount();
        if (emp.resolution == ParsedEmployee::Resolution::Skip) continue;

        // Determine effective employee
        Employee effective;

        const bool createNew =
            emp.resolution == ParsedEmployee::Resolution::CreateNew ||
            emp.wageDecision == ParsedEmployee::WageDecision::CreateNew;

        if (createNew) {
            Employee newEmp;
            newEmp.name = emp.suggestedNewName.trimmed().isEmpty()
                              ? emp.csvName   // fallback — should never happen
                              : emp.suggestedNewName;

            if (emp.csvPayType == PayType::Monthly) {
                newEmp.payType             = PayType::Monthly;
                newEmp.monthlySalary       = emp.wageParseOk ? emp.csvWage
                                           : (emp.manualWage > 0.0 ? emp.manualWage : 0.0);
                newEmp.workingDaysPerMonth = emp.csvWorkingDays;
                newEmp.hourlyWage          = 0.0;
                // Restore expected times from CSV — invalid QTime() if not in file
                newEmp.expectedCheckin  = emp.csvExpectedCheckin;
                newEmp.expectedCheckout = emp.csvExpectedCheckout;
            } else {
                newEmp.payType    = PayType::Hourly;
                newEmp.hourlyWage = emp.wageParseOk ? emp.csvWage
                                  : (emp.manualWage > 0.0 ? emp.manualWage : 0.0);
            }

            if (!EmployeeRepository::instance().addEmployee(newEmp)) {
                // DB-level failure — roll back everything already inserted this run
                db.rollback();
                ImportResult failed;
                failed.failed = pass1.totalSelectedCount();
                failed.warnings.append(
                    tr("Could not create employee \"%1\": %2 \xe2\x80\x94 "
                       "Import aborted. No records were imported.")
                        .arg(emp.suggestedNewName.isEmpty() ? emp.csvName : emp.suggestedNewName)
                        .arg(EmployeeRepository::instance().lastError()));
                return failed;
            }
            result.created++;
            if (emp.csvPayType == PayType::Monthly) {
                result.notices.append(
                    tr("Employee \"%1\" created automatically with monthly salary %2.")
                        .arg(newEmp.name).arg(newEmp.monthlySalary, 0, 'f', 2));
            } else {
                result.notices.append(
                    tr("Employee \"%1\" created automatically with hourly wage %2.")
                        .arg(newEmp.name).arg(newEmp.hourlyWage, 0, 'f', 2));
            }
            effective = newEmp;
        } else {
            if (!emp.existingEmployee.has_value()) {
                // Should never happen — UseExisting/UseExistingWarn always have existingEmployee set
                result.failed += emp.selectedCount();
                result.warnings.append(
                    tr("Internal error: employee \"%1\" has no DB record. Skipped.")
                        .arg(emp.csvName));
                continue;
            }
            effective = *emp.existingEmployee;
            if (emp.resolution == ParsedEmployee::Resolution::UseExistingWarn) {
                const double dbWage = (emp.csvPayType == PayType::Monthly)
                    ? effective.monthlySalary
                    : effective.hourlyWage;
                const QString dec =
                    emp.wageDecision == ParsedEmployee::WageDecision::RecalculateCurrent
                        ? tr("records recalculated at current wage %1")
                              .arg(dbWage, 0, 'f', 2)
                        : tr("original CSV values kept");
                result.notices.append(
                    tr("%1: CSV wage %2 differed from current wage %3 \xe2\x80\x94 %4.")
                        .arg(emp.csvName)
                        .arg(emp.csvWage, 0, 'f', 2)
                        .arg(dbWage, 0, 'f', 2)
                        .arg(dec));
            }
        }

        // Insert selected records
        for (auto& rec : emp.records) {
            if (!rec.selected) continue;
            if (rec.status == ParsedRecord::Status::HardError) continue;

            AttendanceRecord ar;
            ar.employeeId = effective.id;
            ar.date       = rec.date;
            ar.checkIn    = rec.checkIn;
            ar.checkOut   = rec.checkOut;
            ar.paid       = rec.paid;

            if (rec.isOpen) {
                ar.hoursWorked = 0.0;
                ar.dailyWage   = 0.0;
            } else {
                // Monthly employees: always keep the CSV daily wage values.
                // The daily wage in the CSV was already computed at export time
                // using the full monthly calculation (expected times, tolerance,
                // deductions). Recalculating here would require replicating all
                // of that with the current employee config, which may have changed.
                //
                // Hourly employees: recalculate when UseExisting or CreateNew
                // (we trust the current wage), keep CSV values for KeepCsvValues.
                const bool isMonthlyEmp = (emp.csvPayType == PayType::Monthly);

                const bool recalc = !isMonthlyEmp && (
                    (emp.resolution == ParsedEmployee::Resolution::CreateNew) ||
                    (emp.resolution == ParsedEmployee::Resolution::UseExisting) ||
                    (emp.resolution == ParsedEmployee::Resolution::UseExistingWarn &&
                     emp.wageDecision == ParsedEmployee::WageDecision::RecalculateCurrent));

                if (recalc) {
                    ar.calculate(effective.hourlyWage);
                } else {
                    ar.hoursWorked = rec.hoursWorked;
                    ar.dailyWage   = rec.dailyWage;
                    // Restore monthly transparency fields from CSV cols 6 & 7.
                    // For hourly employees these are always 0.0 — harmless.
                    ar.baseDailyRate = rec.baseDailyRate;
                    ar.dayDeduction  = rec.dayDeduction;
                }
            }

            if (AttendanceRepository::instance().addRecord(ar)) {
                result.imported++;
            } else {
                result.skipped++;
                result.warnings.append(
                    tr("Line %1 (%2, %3): %4")
                        .arg(rec.csvRowNumber)
                        .arg(rec.date.toString("yyyy-MM-dd"))
                        .arg(emp.csvName)
                        .arg(AttendanceRepository::instance().lastError()));
            }
        }
    }

    qDebug() << "[Import] committing transaction. imported:" << result.imported;
    if (!db.commit()) {
        qDebug() << "[Import] db.commit() FAILED:" << db.lastError().text();
        db.rollback();
        ImportResult failed;
        failed.failed = pass1.totalSelectedCount();
        failed.warnings.append(
            tr("Database commit failed — no records were imported. Please try again."));
        return failed;
    }

    // Log each employee's import as a single audit entry
    for (const auto& emp : pass1.employees) {
        if (emp.selectedCount() > 0) {
            const QString name = emp.suggestedNewName.trimmed().isEmpty()
                                 ? emp.csvName : emp.suggestedNewName;
            AuditLog::record(AuditLog::IMPORT, "employee", 0,
                QString("Imported %1 records for \"%2\"")
                    .arg(emp.selectedCount()).arg(name));
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API — importAttendance()
// ═══════════════════════════════════════════════════════════════════════════

ImportResult importAttendance(QWidget* parent)
{
    const QString path = QFileDialog::getOpenFileName(
        parent,
        tr("Import Attendance"),
        QDir::homePath(),
        tr("CSV Files (*.csv);;All Files (*)"));

    if (path.isEmpty()) return {};

    qDebug() << "[Import] parseFile starting:" << path;
    ParsePass1Result pass1 = parseFile(path);
    qDebug() << "[Import] parseFile done. employees:" << pass1.employees.size()
             << "hasAnyIssues:" << pass1.hasAnyIssues
             << "hasAnyImportable:" << pass1.hasAnyImportable
             << "fileError:" << pass1.fileError;

    if (!pass1.fileError.isEmpty()) {
        ImportResult err;
        err.failed = 1;
        err.warnings.append(pass1.fileError);
        return err;
    }

    if (!pass1.hasAnyImportable) {
        ImportResult empty;
        empty.warnings.append(tr("No valid records found in this file."));
        return empty;
    }

    if (pass1.hasAnyIssues) {
        qDebug() << "[Import] showing preview dialog";
        ImportPreviewDialog dlg(pass1, parent);
        if (dlg.exec() != QDialog::Accepted)
            return {};
    } else {
        const int total    = pass1.totalSelectedCount();
        const int empCount = static_cast<int>(
            std::count_if(pass1.employees.begin(), pass1.employees.end(),
                [](const ParsedEmployee& e){
                    return e.resolution != ParsedEmployee::Resolution::Skip;
                }));
        qDebug() << "[Import] fast path: total:" << total << "emps:" << empCount;
        const auto btn = QMessageBox::question(
            parent,
            tr("Confirm Import"),
            tr("Import %1 record(s) for %2 employee(s)?").arg(total).arg(empCount));
        if (btn != QMessageBox::Yes)
            return {};
    }

    return commitImport(pass1);
}

} // namespace ImportHelper
