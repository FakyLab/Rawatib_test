#include "utils/ExportHelper.h"
#include "utils/CurrencyManager.h"
#include "utils/SettingsManager.h"
#include "utils/PayrollCalculator.h"
#include "utils/DeductionPolicy.h"
#include "ui/AdvancedTab.h"
#include "ui/dialogs/ExportAllDialog.h"
#include "repositories/AttendanceRepository.h"
#include "repositories/EmployeeRepository.h"
#include "models/AttendanceRecord.h"
#include "models/Employee.h"
#include <cmath>

#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDate>
#include <QLocale>
#include <QGuiApplication>
#include <QLabel>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <QFileInfo>

// ── QXlsx ─────────────────────────────────────────────────────────────────
// If QXlsx is not yet available (third_party/QXlsx not populated),
// XLSX export gracefully degrades to a "not available" message.
// CSV export always works regardless of QXlsx.
#if __has_include(<xlsxdocument.h>)
#  include <xlsxdocument.h>
#  include <xlsxformat.h>
#  include <xlsxworksheet.h>
#  include <xlsxcellformula.h>
#  define QXLSX_AVAILABLE 1
#else
#  define QXLSX_AVAILABLE 0
#endif

// ── Translation shorthand ─────────────────────────────────────────────────
static inline QString tr(const char* key) {
    return QCoreApplication::translate("ExportHelper", key);
}

static inline bool isRtl() {
    return QGuiApplication::layoutDirection() == Qt::RightToLeft;
}

// Returns the QLocale matching the active UI language.
// Used for XLSX and print output — month names appear in the user's language.
// The CSV path uses hardcoded English strings directly (not this function)
// so that imported files always have a stable, predictable structure.
static inline QLocale appLocale() {
    const QString lang = SettingsManager::getLanguage();
    if (lang == "ar") return QLocale(QLocale::Arabic);
    if (lang == "fr") return QLocale(QLocale::French);
    if (lang == "tr") return QLocale(QLocale::Turkish);
    if (lang == "zh") return QLocale(QLocale::Chinese);
    if (lang == "ja") return QLocale(QLocale::Japanese);
    if (lang == "ko") return QLocale(QLocale::Korean);
    return QLocale::c();   // English / any unmapped language
}

// ── Sheet name sanitization ───────────────────────────────────────────────
// Excel sheet names: max 31 chars, no \ / ? * [ ] :
static QString safeSheetName(const QString& name) {
    QString s = name;
    for (QChar c : {u'\\', u'/', u'?', u'*', u'[', u']', u':'})
        s.remove(c);
    if (s.length() > 31)
        s = s.left(28) + "...";
    return s;
}

// ── Default export filename ───────────────────────────────────────────────
static QString defaultFilename(const QString& employeeName, int month, int year,
                                const QString& ext) {
    QString base = employeeName.simplified().replace(' ', '_');
    // Always use English month names in filenames — consistent across all UI languages.
    QString monthName = QLocale::c().monthName(month, QLocale::LongFormat);
    return QString("%1_%2_%3.%4").arg(base).arg(monthName).arg(year).arg(ext);
}

// ═════════════════════════════════════════════════════════════════════════
// CSV implementation
// ═════════════════════════════════════════════════════════════════════════

// Escape a CSV field: wrap in quotes if it contains comma, quote, or newline
static QString csvField(const QString& s) {
    if (s.contains(',') || s.contains('"') || s.contains('\n')) {
        return '"' + QString(s).replace('"', "\"\"") + '"';
    }
    return s;
}

static void writeCsvRow(QTextStream& out, const QStringList& fields) {
    QStringList escaped;
    for (const auto& f : fields)
        escaped << csvField(f);
    out << escaped.join(',') << '\n';
}

// Write one employee's data block into the stream.
// Used by both single and all-employees CSV.
static void writeCsvEmployeeBlock(QTextStream& out,
                                   const QString& employeeName,
                                   int month, int year,
                                   const Employee& employee,
                                   const QVector<AttendanceRecord>& records,
                                   const MonthlySummary& summary,
                                   const PayrollCalculator::Result& payrollResult,
                                   bool wagesVisible)
{
    const QLocale loc = appLocale();

    // Header block
    // NOTE: All CSV header labels and fixed strings are hardcoded in English.
    // This ensures ImportHelper can always parse exported files regardless of
    // the active UI language.  Only the Period value uses an English month name
    // for the same reason.
    const QString englishMonthName = QLocale::c().monthName(month, QLocale::LongFormat);
    out << "Rawatib - Monthly Attendance Report" << '\n';
    out << "Employee" << "," << csvField(employeeName) << '\n';
    out << "Period" << ","
        << csvField(englishMonthName + " " + QString::number(year)) << '\n';
    if (wagesVisible) {
        if (employee.isMonthly()) {
            out << "Monthly Salary" << ","
                << csvField(CurrencyManager::format(employee.monthlySalary)) << '\n';
            out << "Monthly Salary Raw" << ","
                << QString::number(employee.monthlySalary, 'f', 2) << '\n';
            out << "Working Days/Month" << ","
                << QString::number(employee.workingDaysPerMonth) << '\n';
            // Expected times — empty string if not set (flexible schedule)
            out << "Expected Check-In" << ","
                << (employee.expectedCheckin.isValid()
                    ? employee.expectedCheckin.toString("HH:mm") : QString()) << '\n';
            out << "Expected Check-Out" << ","
                << (employee.expectedCheckout.isValid()
                    ? employee.expectedCheckout.toString("HH:mm") : QString()) << '\n';
            // Deduction mode — informational, not parsed by import
            const DeductionPolicy::Mode dmode = DeductionPolicy::mode();
            QString modeStr = DeductionPolicy::modeToString(dmode);
            if (dmode == DeductionPolicy::Mode::PerDay)
                modeStr += QString(":%1").arg(DeductionPolicy::perDayPenaltyPct(), 0, 'f', 1);
            out << "Deduction Mode" << "," << modeStr << '\n';
        } else {
            out << "Hourly Wage" << ","
                << csvField(CurrencyManager::format(employee.hourlyWage)) << '\n';
            out << "Hourly Wage Raw" << ","
                << QString::number(employee.hourlyWage, 'f', 2) << '\n';
        }
    }
    out << "Exported" << ","
        << QDate::currentDate().toString("yyyy-MM-dd") << '\n';
    out << '\n';

    // Column headers
    if (employee.isMonthly()) {
        writeCsvRow(out, {
            "Date", "Check-In", "Check-Out", "Hours",
            "Net Day", "Status", "Base Rate", "Deduction"
        });
    } else {
        writeCsvRow(out, {
            "Date", "Check-In", "Check-Out", "Hours",
            "Daily Wage", "Status"
        });
    }

    // Data rows
    for (const auto& r : records) {
        if (r.isOpen()) {
            if (employee.isMonthly()) {
                writeCsvRow(out, {
                    r.date.toString("yyyy-MM-dd"),
                    r.checkIn.toString("HH:mm"),
                    "--", "--", "--", "Open", "--", "--"
                });
            } else {
                writeCsvRow(out, {
                    r.date.toString("yyyy-MM-dd"),
                    r.checkIn.toString("HH:mm"),
                    "--", "--", "--", "Open"
                });
            }
        } else {
            if (employee.isMonthly()) {
                writeCsvRow(out, {
                    r.date.toString("yyyy-MM-dd"),
                    r.checkIn.toString("HH:mm"),
                    r.checkOut.toString("HH:mm"),
                    QString::number(r.hoursWorked, 'f', 2),
                    wagesVisible ? QString::number(r.dailyWage,     'f', 2) : QStringLiteral("--"),
                    r.paid ? "Paid" : "Unpaid",
                    wagesVisible ? QString::number(r.baseDailyRate, 'f', 2) : QStringLiteral("--"),
                    wagesVisible ? QString::number(r.dayDeduction,  'f', 2) : QStringLiteral("--")
                });
            } else {
                writeCsvRow(out, {
                    r.date.toString("yyyy-MM-dd"),
                    r.checkIn.toString("HH:mm"),
                    r.checkOut.toString("HH:mm"),
                    QString::number(r.hoursWorked, 'f', 2),
                    wagesVisible ? QString::number(r.dailyWage, 'f', 2) : QStringLiteral("--"),
                    r.paid ? "Paid" : "Unpaid"
                });
            }
        }
    }

    // Summary block
    out << '\n';
    out << "Summary" << '\n';
    writeCsvRow(out, { "Total Days", QString::number(summary.totalDays) });
    writeCsvRow(out, { "Total Hours", QString::number(summary.totalHours, 'f', 2) });
    if (wagesVisible) {
        writeCsvRow(out, { "Total Salary", QString::number(summary.totalSalary, 'f', 2) });
        writeCsvRow(out, { "Paid Amount", QString::number(summary.paidAmount, 'f', 2) });
        writeCsvRow(out, { "Unpaid Remaining", QString::number(summary.unpaidAmount, 'f', 2) });

        // Net pay block — only when payroll rules are active
        if (payrollResult.grossPay > 0 && !payrollResult.breakdown.isEmpty()) {
            out << '\n';
            out << "Payroll Adjustments" << '\n';
            for (const auto& item : payrollResult.breakdown) {
                const QString sign = item.amount < 0 ? "-" : "+";
                writeCsvRow(out, {
                    item.name,
                    sign + QString::number(std::abs(item.amount), 'f', 2)
                });
            }
            writeCsvRow(out, { "Net Pay", QString::number(payrollResult.netPay, 'f', 2) });
        }
    }
    out << '\n';
}

static bool writeCsvSingle(const QString& path,
                            const QString& employeeName,
                            int employeeId,
                            int month, int year,
                            const Employee& employee,
                            const PayrollCalculator::Result& payrollResult,
                            bool wagesVisible)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "\xEF\xBB\xBF";

    auto records = AttendanceRepository::instance()
                       .getRecordsForMonth(employeeId, year, month);
    auto summary = AttendanceRepository::instance()
                       .getMonthlySummary(employeeId, year, month);

    writeCsvEmployeeBlock(out, employeeName, month, year, employee,
                          records, summary, payrollResult, wagesVisible);
    file.close();
    return true;
}



// ═════════════════════════════════════════════════════════════════════════
// XLSX implementation
// ═════════════════════════════════════════════════════════════════════════

#if QXLSX_AVAILABLE

// ── Format helpers ────────────────────────────────────────────────────────

// Info block value cell — left-aligned so value sits at reading edge
// inside the merged span rather than floating to the far edge
static QXlsx::Format fmtInfoValue(bool currency = false) {
    QXlsx::Format f;
    f.setFontSize(10);
    f.setHorizontalAlignment(isRtl()
        ? QXlsx::Format::AlignRight
        : QXlsx::Format::AlignLeft);
    if (currency)
        f.setNumberFormat("#,##0.00");
    return f;
}

// Apply all-borders (thin) to any format — call on every format before writing
static QXlsx::Format withBorder(QXlsx::Format f) {
    f.setBorderStyle(QXlsx::Format::BorderThin);
    return f;
}

// Dark navy header — used for section title rows (centered)
static QXlsx::Format fmtTitleRow() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setFontColor(QColor("#FFFFFF"));
    f.setPatternBackgroundColor(QColor("#1F3864"));
    f.setFontSize(11);
    f.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    return f;
}

// Blue column header row
static QXlsx::Format fmtColHeader() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setFontColor(QColor("#FFFFFF"));
    f.setPatternBackgroundColor(QColor("#2E4D7B"));
    f.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    f.setFontSize(10);
    return f;
}

// Label cell in summary / info block (grey background)
static QXlsx::Format fmtLabel() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setPatternBackgroundColor(QColor("#EEF0F2"));
    f.setFontSize(10);
    if (isRtl())
        f.setHorizontalAlignment(QXlsx::Format::AlignRight);
    return f;
}

// Normal data cell
static QXlsx::Format fmtData(bool altRow = false) {
    QXlsx::Format f;
    f.setFontSize(10);
    if (altRow)
        f.setPatternBackgroundColor(QColor("#F7F9FA"));
    if (isRtl())
        f.setHorizontalAlignment(QXlsx::Format::AlignRight);
    return f;
}

// Centred data cell
static QXlsx::Format fmtDataCenter(bool altRow = false) {
    QXlsx::Format f = fmtData(altRow);
    f.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    return f;
}

// Paid status cell — green
static QXlsx::Format fmtPaid() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setFontColor(QColor("#1E8449"));
    f.setPatternBackgroundColor(QColor("#E9F7EF"));
    f.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    f.setFontSize(10);
    return f;
}

// Unpaid status cell — red
static QXlsx::Format fmtUnpaid() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setFontColor(QColor("#C0392B"));
    f.setPatternBackgroundColor(QColor("#FDF0EE"));
    f.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    f.setFontSize(10);
    return f;
}

// Summary label — used for summary section labels
static QXlsx::Format fmtSummaryLabel() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setPatternBackgroundColor(QColor("#EEF0F2"));
    f.setFontSize(10);
    if (isRtl())
        f.setHorizontalAlignment(QXlsx::Format::AlignRight);
    return f;
}

// Unpaid summary value — red highlight
static QXlsx::Format fmtUnpaidSummary() {
    QXlsx::Format f;
    f.setFontBold(true);
    f.setFontColor(QColor("#C0392B"));
    f.setPatternBackgroundColor(QColor("#FDF0EE"));
    f.setFontSize(10);
    return f;
}

// Number format for currency values
static QXlsx::Format fmtCurrency() {
    QXlsx::Format f;
    f.setFontSize(10);
    f.setNumberFormat("#,##0.00");
    return f;
}

static QXlsx::Format fmtCurrencyUnpaid() {
    QXlsx::Format f = fmtUnpaidSummary();
    f.setNumberFormat("#,##0.00");
    return f;
}

// ── Write one employee sheet ───────────────────────────────────────────────
// Layout is driven entirely by named constants — change a constant and
// every row reference below shifts automatically with no magic numbers.
//
// Columns: A=Date, B=Check-In, C=Check-Out, D=Hours, E=Wage, F=Status
// Hourly wage is in B(ROW_WAGE) so formulas reference it as $B$<ROW_WAGE>.

static void writeEmployeeSheet(QXlsx::Document& doc,
                                const QString& sheetName,
                                const QString& employeeName,
                                int month, int year,
                                const Employee& employee,
                                int employeeId,
                                const PayrollCalculator::Result& payrollResult,
                                bool wagesVisible = true,
                                const QVector<AttendanceRecord>& preloadedRecords = {})
{
    // addSheet returns null on failure (duplicate name etc.).
    // Always select by index — the new sheet is always last.
    // This avoids any name-transformation mismatch between our sheetName
    // and what QXlsx's createSafeSheetName() stores internally.
    doc.addSheet(sheetName);
    doc.selectSheet(doc.sheetNames().size() - 1);

    // Capture worksheet pointer once — all writeFormula calls use this
    // directly so there is no repeated null-check overhead and no
    // possibility of the active sheet changing mid-function.
    QXlsx::Worksheet* ws = doc.currentWorksheet();
    if (!ws) return;   // addSheet failed — bail safely

    if (isRtl())
        ws->setRightToLeft(true);

    const QLocale loc = appLocale();
    const QString periodStr = (year == 0)
        ? tr("All Records")
        : loc.monthName(month, QLocale::LongFormat) + " " + QString::number(year);

    // ── Layout constants ──────────────────────────────────────────────────
    constexpr int ROW_TITLE      = 1;
    constexpr int ROW_BLANK_1    = 2;
    constexpr int ROW_EMPLOYEE   = 3;
    constexpr int ROW_PERIOD     = 4;
    constexpr int ROW_WAGE       = 5;   // ← formula anchor $B$5 for hourly
    constexpr int ROW_EXPORTED   = 6;
    // Monthly employees get extra rows: deduction mode, expected check-in/out.
    // Hourly employees skip them — their layout stays at the same positions.
    const bool isMonthly       = employee.isMonthly();
    const int ROW_DED_MODE     = isMonthly ? 7  : 0;
    const int ROW_EXP_CI       = isMonthly ? 8  : 0;   // expected check-in
    const int ROW_EXP_CO       = isMonthly ? 9  : 0;   // expected check-out
    const int ROW_BLANK_2      = isMonthly ? 10 : 7;
    const int ROW_HEADERS      = isMonthly ? 11 : 8;
    const int ROW_DATA_START   = isMonthly ? 12 : 9;

    constexpr int NUM_COLS = 6;         // A through F (hourly); A through H (monthly)

    // For monthly employees the sheet has 8 columns (A–H).
    // All merged rows — title, spacers, info block, summary — span the full width.
    const QString lastColLetter = isMonthly ? "H" : "F";

    // Helper: build "A%1:H%1" (monthly) or "A%1:F%1" (hourly)
    auto fullRow = [&](int r) {
        return QString("A%1:%2%3").arg(r).arg(lastColLetter).arg(r);
    };
    // Helper: build "B%1:H%1" (monthly) or "B%1:F%1" (hourly) for merged value cells
    auto valueSpan = [&](int r) {
        return QString("B%1:%2%3").arg(r).arg(lastColLetter).arg(r);
    };
    // Wage formula reference uses ROW_WAGE as the anchor row
    auto wageFormula = [&](int dataRow) {
        return QString("=D%1*$B$%2").arg(dataRow).arg(ROW_WAGE);
    };

    // ── Column widths ─────────────────────────────────────────────────────
    doc.setColumnWidth(1, 22);   // A: Date / label
    doc.setColumnWidth(2, 11);   // B: Check-In / value
    doc.setColumnWidth(3, 11);   // C: Check-Out
    doc.setColumnWidth(4, 9);    // D: Hours
    doc.setColumnWidth(5, 14);   // E: Net Day (monthly) / Daily Wage (hourly)
    doc.setColumnWidth(6, 11);   // F: Status
    if (employee.isMonthly()) {
        doc.setColumnWidth(7, 14);   // G: Base Rate (monthly only)
        doc.setColumnWidth(8, 14);   // H: Deduction (monthly only)
    }

    // ── Row 1: Title ──────────────────────────────────────────────────────
    doc.mergeCells(fullRow(ROW_TITLE), withBorder(fmtTitleRow()));
    doc.write(ROW_TITLE, 1, tr("Rawatib — Monthly Attendance Report"),
              withBorder(fmtTitleRow()));

    // ── Row 2: Blank spacer ───────────────────────────────────────────────
    doc.mergeCells(fullRow(ROW_BLANK_1), withBorder(fmtData()));

    // ── Rows 3–6: Info block ──────────────────────────────────────────────
    doc.write(ROW_EMPLOYEE, 1, tr("Employee"),    withBorder(fmtLabel()));
    doc.mergeCells(valueSpan(ROW_EMPLOYEE),        withBorder(fmtInfoValue()));
    doc.write(ROW_EMPLOYEE, 2, employeeName,       withBorder(fmtInfoValue()));

    doc.write(ROW_PERIOD, 1, tr("Period"),         withBorder(fmtLabel()));
    doc.mergeCells(valueSpan(ROW_PERIOD),           withBorder(fmtInfoValue()));
    doc.write(ROW_PERIOD, 2, periodStr,             withBorder(fmtInfoValue()));

    if (employee.isMonthly()) {
        doc.write(ROW_WAGE, 1, tr("Monthly Salary"),   withBorder(fmtLabel()));
        doc.mergeCells(valueSpan(ROW_WAGE),             withBorder(fmtInfoValue(true)));
        doc.write(ROW_WAGE, 2, employee.monthlySalary,  withBorder(fmtInfoValue(true)));
    } else {
        doc.write(ROW_WAGE, 1, tr("Hourly Wage"),      withBorder(fmtLabel()));
        doc.mergeCells(valueSpan(ROW_WAGE),             withBorder(fmtInfoValue(true)));
        doc.write(ROW_WAGE, 2, employee.hourlyWage,     withBorder(fmtInfoValue(true)));
    }

    doc.write(ROW_EXPORTED, 1, tr("Exported"),     withBorder(fmtLabel()));
    doc.mergeCells(valueSpan(ROW_EXPORTED),         withBorder(fmtInfoValue()));
    doc.write(ROW_EXPORTED, 2,
              QDate::currentDate().toString("yyyy-MM-dd"), withBorder(fmtInfoValue()));

    // ── Deduction mode row (monthly only) ─────────────────────────────────
    if (isMonthly && ROW_DED_MODE > 0) {
        const DeductionPolicy::Mode dmode = DeductionPolicy::mode();
        QString modeStr = DeductionPolicy::modeLabel(dmode);
        if (dmode == DeductionPolicy::Mode::PerDay)
            modeStr += QString(" (%1%)").arg(DeductionPolicy::perDayPenaltyPct(), 0, 'f', 1);
        doc.write(ROW_DED_MODE, 1, tr("Deduction Mode"), withBorder(fmtLabel()));
        doc.mergeCells(valueSpan(ROW_DED_MODE),           withBorder(fmtInfoValue()));
        doc.write(ROW_DED_MODE, 2, modeStr,               withBorder(fmtInfoValue()));
    }

    // ── Expected check-in / check-out rows (monthly only) ─────────────────
    if (isMonthly && ROW_EXP_CI > 0) {
        const QString ciStr = employee.expectedCheckin.isValid()
                              ? employee.expectedCheckin.toString("hh:mm AP")
                              : tr("Not set");
        const QString coStr = employee.expectedCheckout.isValid()
                              ? employee.expectedCheckout.toString("hh:mm AP")
                              : tr("Not set");
        doc.write(ROW_EXP_CI, 1, tr("Expected Check-In"),  withBorder(fmtLabel()));
        doc.mergeCells(valueSpan(ROW_EXP_CI),               withBorder(fmtInfoValue()));
        doc.write(ROW_EXP_CI, 2, ciStr,                     withBorder(fmtInfoValue()));
        doc.write(ROW_EXP_CO, 1, tr("Expected Check-Out"), withBorder(fmtLabel()));
        doc.mergeCells(valueSpan(ROW_EXP_CO),               withBorder(fmtInfoValue()));
        doc.write(ROW_EXP_CO, 2, coStr,                     withBorder(fmtInfoValue()));
    }

    // ── Blank spacer ──────────────────────────────────────────────────────
    doc.mergeCells(fullRow(ROW_BLANK_2), withBorder(fmtData()));

    // ── Column headers ────────────────────────────────────────────────────
    doc.write(ROW_HEADERS, 1, tr("Date"),        withBorder(fmtColHeader()));
    doc.write(ROW_HEADERS, 2, tr("Check-In"),    withBorder(fmtColHeader()));
    doc.write(ROW_HEADERS, 3, tr("Check-Out"),   withBorder(fmtColHeader()));
    doc.write(ROW_HEADERS, 4, tr("Hours"),       withBorder(fmtColHeader()));
    doc.write(ROW_HEADERS, 5,
              (employee.isMonthly() ? tr("Net Day") : tr("Wage"))
              + " (" + CurrencyManager::symbol() + ")",
              withBorder(fmtColHeader()));
    doc.write(ROW_HEADERS, 6, tr("Status"), withBorder(fmtColHeader()));
    if (employee.isMonthly()) {
        doc.write(ROW_HEADERS, 7,
                  tr("Base Rate") + " (" + CurrencyManager::symbol() + ")",
                  withBorder(fmtColHeader()));
        doc.write(ROW_HEADERS, 8,
                  tr("Deduction") + " (" + CurrencyManager::symbol() + ")",
                  withBorder(fmtColHeader()));
    }

    // ── Data rows ─────────────────────────────────────────────────────────
    auto records = preloadedRecords.isEmpty()
                       ? AttendanceRepository::instance()
                             .getRecordsForMonth(employeeId, year, month)
                       : preloadedRecords;

    int row = ROW_DATA_START;
    bool alt = false;

    for (const auto& r : records) {
        const bool paid = r.paid;
        const bool open = r.isOpen();

        doc.write(row, 1, r.date.toString("yyyy-MM-dd"), withBorder(fmtData(alt)));
        doc.write(row, 2, r.checkIn.toString("HH:mm"),   withBorder(fmtDataCenter(alt)));

        if (open) {
            doc.write(row, 3, "--",       withBorder(fmtDataCenter(alt)));
            doc.write(row, 4, "--",       withBorder(fmtDataCenter(alt)));
            doc.write(row, 5, "--",       withBorder(fmtDataCenter(alt)));
            if (employee.isMonthly()) {
                doc.write(row, 6, tr("Open"), withBorder(fmtDataCenter(alt)));
                doc.write(row, 7, "--",       withBorder(fmtDataCenter(alt)));
                doc.write(row, 8, "--",       withBorder(fmtDataCenter(alt)));
            } else {
                doc.write(row, 6, tr("Open"), withBorder(fmtDataCenter(alt)));
            }
        } else {
            doc.write(row, 3, r.checkOut.toString("HH:mm"), withBorder(fmtDataCenter(alt)));

            QXlsx::Format hFmt = fmtDataCenter(alt);
            hFmt.setNumberFormat("0.00");
            doc.write(row, 4, r.hoursWorked, withBorder(hFmt));

            QXlsx::Format wFmt = fmtCurrency();
            if (alt) wFmt.setPatternBackgroundColor(QColor("#F7F9FA"));
            if (employee.isMonthly()) {
                // Col 5: Net Day, Col 6: Status, Col 7: Base Rate, Col 8: Deduction
                doc.write(row, 5, r.dailyWage,     withBorder(wFmt));
                doc.write(row, 6,
                    r.paid ? tr("Paid") : tr("Unpaid"),
                    withBorder(r.paid ? fmtPaid() : fmtUnpaid()));
                doc.write(row, 7, r.baseDailyRate, withBorder(wFmt));
                doc.write(row, 8, r.dayDeduction,  withBorder(wFmt));
            } else {
                ws->writeFormula(row, 5,
                    QXlsx::CellFormula(wageFormula(row)),
                    withBorder(wFmt), r.dailyWage);
                doc.write(row, 6,
                    r.paid ? tr("Paid") : tr("Unpaid"),
                    withBorder(r.paid ? fmtPaid() : fmtUnpaid()));
            }
        }

        ++row;
        alt = !alt;
    }

    // ── Calculated row positions ──────────────────────────────────────────
    // dataEndRow: last row that has data. If no records, ROW_DATA_START itself
    // (the "no records" message will sit there).
    const int dataEndRow      = records.isEmpty() ? ROW_DATA_START : row - 1;
    const int ROW_BLANK_3     = dataEndRow + 1;   // spacer before summary
    const int ROW_SUMMARY     = dataEndRow + 2;   // summary title row

    // COUNTA/SUM formula ranges span from ROW_DATA_START to dataEndRow
    const QString dataRange_A = QString("A%1:A%2").arg(ROW_DATA_START).arg(dataEndRow);
    const QString dataRange_D = QString("D%1:D%2").arg(ROW_DATA_START).arg(dataEndRow);
    const QString dataRange_E = QString("E%1:E%2").arg(ROW_DATA_START).arg(dataEndRow);
    const QString dataRange_F = QString("F%1:F%2").arg(ROW_DATA_START).arg(dataEndRow);

    if (records.isEmpty()) {
        doc.mergeCells(fullRow(ROW_DATA_START), withBorder(fmtData()));
        doc.write(ROW_DATA_START, 1,
                  tr("No attendance records for this period."), withBorder(fmtData()));
    }

    // ── Blank spacer before summary ───────────────────────────────────────
    doc.mergeCells(fullRow(ROW_BLANK_3), withBorder(fmtData()));

    // ── Summary block ─────────────────────────────────────────────────────
    const int sr = ROW_SUMMARY;

    auto summary = AttendanceRepository::instance()
                       .getMonthlySummary(employeeId, year, month);

    // Section title
    doc.mergeCells(fullRow(sr), withBorder(fmtTitleRow()));
    doc.write(sr, 1, tr("Summary"), withBorder(fmtTitleRow()));

    // Total Days — COUNTA
    doc.write(sr+1, 1, tr("Total Days Worked"), withBorder(fmtSummaryLabel()));
    doc.mergeCells(valueSpan(sr+1), withBorder(fmtData()));
    if (records.isEmpty()) {
        doc.write(sr+1, 2, 0, withBorder(fmtData()));
    } else {
        ws->writeFormula(sr+1, 2,
            QXlsx::CellFormula("=COUNTA(" + dataRange_A + ")"),
            withBorder(fmtData()), summary.totalDays);
    }

    // Total Hours — SUM
    doc.write(sr+2, 1, tr("Total Hours Worked"), withBorder(fmtSummaryLabel()));
    QXlsx::Format hSumFmt = fmtData();
    hSumFmt.setNumberFormat("0.00");
    doc.mergeCells(valueSpan(sr+2), withBorder(hSumFmt));
    if (records.isEmpty()) {
        doc.write(sr+2, 2, 0.0, withBorder(hSumFmt));
    } else {
        ws->writeFormula(sr+2, 2,
            QXlsx::CellFormula("=SUM(" + dataRange_D + ")"),
            withBorder(hSumFmt), summary.totalHours);
    }

    // Total Salary — SUM (only when wages are visible)
    if (wagesVisible) {
        doc.write(sr+3, 1, tr("Total Salary"), withBorder(fmtSummaryLabel()));
        doc.mergeCells(valueSpan(sr+3), withBorder(fmtCurrency()));
        if (records.isEmpty()) {
            doc.write(sr+3, 2, 0.0, withBorder(fmtCurrency()));
        } else {
            ws->writeFormula(sr+3, 2,
                QXlsx::CellFormula("=SUM(" + dataRange_E + ")"),
                withBorder(fmtCurrency()), summary.totalSalary);
        }

        // Paid Amount — SUMIF
        doc.write(sr+4, 1, tr("Paid Amount"), withBorder(fmtSummaryLabel()));
        doc.mergeCells(valueSpan(sr+4), withBorder(fmtCurrency()));
        if (records.isEmpty()) {
            doc.write(sr+4, 2, 0.0, withBorder(fmtCurrency()));
        } else {
            ws->writeFormula(sr+4, 2,
                QXlsx::CellFormula(
                    QString("=SUMIF(%1,\"%2\",%3)")
                        .arg(dataRange_F).arg(tr("Paid")).arg(dataRange_E)),
                withBorder(fmtCurrency()), summary.paidAmount);
        }

        // Unpaid Remaining — B(sr+3) - B(sr+4)
        const QXlsx::Format unpaidLabelFmt = withBorder(
            summary.unpaidAmount > 0 ? fmtUnpaidSummary() : fmtSummaryLabel());
        doc.write(sr+5, 1, tr("Unpaid Remaining"), unpaidLabelFmt);
        QXlsx::Format unpaidFmt = summary.unpaidAmount > 0
                                      ? fmtCurrencyUnpaid() : fmtCurrency();
        doc.mergeCells(valueSpan(sr+5), withBorder(unpaidFmt));
        ws->writeFormula(sr+5, 2,
            QXlsx::CellFormula(QString("=B%1-B%2").arg(sr+3).arg(sr+4)),
            withBorder(unpaidFmt), summary.unpaidAmount);
    }

    // ── Net pay section (only when payroll rules are enabled + wages visible) ──
    if (wagesVisible && payrollResult.grossPay > 0 && !payrollResult.breakdown.isEmpty()) {
        int pr = sr + 7;   // leave one blank row gap

        // Section title
        doc.mergeCells(fullRow(pr), withBorder(fmtTitleRow()));
        doc.write(pr, 1, tr("Payroll Adjustments"), withBorder(fmtTitleRow()));
        ++pr;

        for (const auto& item : payrollResult.breakdown) {
            const bool isDeduction = item.amount < 0;
            QXlsx::Format labelFmt = fmtSummaryLabel();
            QXlsx::Format valFmt   = fmtCurrency();
            if (isDeduction) {
                labelFmt.setFontColor(QColor("#C0392B"));
                valFmt.setFontColor(QColor("#C0392B"));
            } else {
                labelFmt.setFontColor(QColor("#1E8449"));
                valFmt.setFontColor(QColor("#1E8449"));
            }
            doc.write(pr, 1, item.name, withBorder(labelFmt));
            doc.mergeCells(valueSpan(pr), withBorder(valFmt));
            doc.write(pr, 2, item.amount, withBorder(valFmt));
            ++pr;
        }

        // Net pay total
        QXlsx::Format netLabelFmt = fmtTitleRow();
        QXlsx::Format netValFmt   = fmtTitleRow();
        netValFmt.setNumberFormat("#,##0.00");
        doc.write(pr, 1, tr("Net Pay"), withBorder(netLabelFmt));
        doc.mergeCells(valueSpan(pr), withBorder(netValFmt));
        doc.write(pr, 2, payrollResult.netPay, withBorder(netValFmt));
    }
}

static bool writeXlsxSingle(const QString& path,
                              const QString& employeeName,
                              int employeeId,
                              int month, int year,
                              const Employee& employee,
                              const PayrollCalculator::Result& payrollResult,
                              bool wagesVisible)
{
    auto* doc = new QXlsx::Document();

    if (!doc->sheetNames().isEmpty())
        doc->deleteSheet(doc->sheetNames().first());

    writeEmployeeSheet(*doc,
                       safeSheetName(employeeName),
                       employeeName,
                       month, year,
                       employee,
                       employeeId,
                       payrollResult,
                       wagesVisible);

    bool result = doc->saveAs(path);
    delete doc;
    return result;
}


#endif // QXLSX_AVAILABLE


// ═════════════════════════════════════════════════════════════════════════
// Public API
// ═════════════════════════════════════════════════════════════════════════

namespace ExportHelper {

void exportMonth(const QString& employeeName,
                 int employeeId,
                 int month, int year,
                 const Employee& employee,
                 const PayrollCalculator::Result& payrollResult,
                 QWidget* parent,
                 bool wagesVisible)
{
    const QString suggested = defaultFilename(employeeName, month, year, "xlsx");

#if QXLSX_AVAILABLE
    const QString filter = tr("Excel Workbook (*.xlsx);;CSV File (*.csv)");
#else
    const QString filter = tr("CSV File (*.csv)");
#endif

    const QString path = QFileDialog::getSaveFileName(
        parent,
        tr("Export Report"),
        QDir::homePath() + "/" + suggested,
        filter);

    if (path.isEmpty()) return;

    bool ok = false;

    if (path.endsWith(".xlsx", Qt::CaseInsensitive)) {
#if QXLSX_AVAILABLE
        ok = writeXlsxSingle(path, employeeName, employeeId,
                              month, year, employee, payrollResult, wagesVisible);
#else
        QMessageBox::warning(parent, tr("Not Available"),
            tr("XLSX export requires QXlsx.\n"
               "See third_party/QXlsx/README.md for setup instructions."));
        return;
#endif
    } else {
        ok = writeCsvSingle(path, employeeName, employeeId,
                            month, year, employee, payrollResult, wagesVisible);
    }

    if (ok) {
        QMessageBox msgBox(parent);
        msgBox.setWindowTitle(tr("Export Successful"));
        msgBox.setText(tr("Report exported successfully to:\n%1").arg(path));
        msgBox.setIcon(QMessageBox::Information);

        QPushButton* openBtn = msgBox.addButton(
            tr("Open Folder"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);

        msgBox.exec();

        if (msgBox.clickedButton() == openBtn) {
            // Reveal the exported file in the system file manager,
            // with the file selected where the platform supports it.
            //
            // Windows : explorer /select,"<path>"  — selects the file
            // macOS   : open -R "<path>"           — Finder reveals the file
            // Linux   : open the parent folder via QDesktopServices
            //           (no universal "select file" standard across DEs)
#if defined(Q_OS_WIN)
            // QDir::toNativeSeparators converts forward slashes to backslashes
            // which explorer.exe requires for the /select flag to work correctly.
            QProcess::startDetached(
                "explorer.exe",
                { "/select,", QDir::toNativeSeparators(path) });
#elif defined(Q_OS_MACOS)
            QProcess::startDetached("open", { "-R", path });
#else
            // Linux / other Unix: open the containing folder.
            // File selection is not standardised across desktop environments.
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
        }
    } else {
        QMessageBox::critical(parent, tr("Export Failed"),
            tr("Could not write file to:\n%1\n\n"
               "Check that the folder is writable.").arg(path));
    }
}

// ── exportAll ─────────────────────────────────────────────────────────────
//
// Exports attendance records for every employee.
//
// Flow:
//   1. Show ExportAllDialog — user picks period (month/year or All Time)
//      and format (CSV or XLSX).
//   2. Show save-file dialog for the output path.
//   3. Fetch all employees; skip employees with no records in the period.
//   4. Write sequential employee blocks into the single output file.
//      CSV  — one block per employee, same format as exportMonth().
//             Fully compatible with ImportHelper::parseFile().
//      XLSX — one worksheet per employee, named after the employee.
//   5. Show success dialog with "Open Folder" button.
//
// All Time mode (month == 0, year == 0):
//   Uses getRecordsForEmployee() to fetch all records across all months.
//   The CSV Period header reads "All Time" — the importer ignores the Period
//   value and determines pay type from the wage header rows instead.

void exportAll(QWidget* parent, bool wagesVisible)
{
    // ── Step 1: Show ExportAllDialog ──────────────────────────────────────
    ExportAllDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return;

    const bool allTime = dlg.isAllTime();
    const int  month   = dlg.selectedMonth();   // 0 if allTime
    const int  year    = dlg.selectedYear();    // 0 if allTime
    const bool useCsv  = dlg.isCsv();

    // ── Step 2: Build suggested filename and show save dialog ─────────────
    QString periodPart;
    if (allTime) {
        periodPart = "All_Time";
    } else {
        periodPart = QLocale::c().monthName(month, QLocale::LongFormat)
                     + "_" + QString::number(year);
    }
    const QString ext       = useCsv ? "csv" : "xlsx";
    const QString suggested = QString("All_Employees_%1.%2").arg(periodPart).arg(ext);

#if QXLSX_AVAILABLE
    const QString filter = useCsv
        ? tr("CSV File (*.csv)")
        : tr("Excel Workbook (*.xlsx)");
#else
    const QString filter = tr("CSV File (*.csv)");
#endif

    const QString path = QFileDialog::getSaveFileName(
        parent,
        tr("Export All Employees"),
        QDir::homePath() + "/" + suggested,
        filter);

    if (path.isEmpty()) return;

    // ── Step 3: Fetch all employees ───────────────────────────────────────
    const QVector<Employee> employees =
        EmployeeRepository::instance().getAllEmployees();

    if (employees.isEmpty()) {
        QMessageBox::information(parent,
            tr("Export All Employees"),
            tr("No employees found."));
        return;
    }

    // ── Step 4: Write output ──────────────────────────────────────────────
    bool ok = false;
    int  employeesExported = 0;
    int  recordsExported   = 0;

    if (useCsv) {
        // ── CSV path ──────────────────────────────────────────────────────
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(parent, tr("Export Failed"),
                tr("Could not write file to:\n%1\n\n"
                   "Check that the folder is writable.").arg(path));
            return;
        }

        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "\xEF\xBB\xBF";   // UTF-8 BOM — Excel compatibility

        for (const auto& emp : employees) {
            QVector<AttendanceRecord> records;
            if (allTime) {
                records = AttendanceRepository::instance()
                              .getRecordsForEmployee(emp.id);
            } else {
                records = AttendanceRepository::instance()
                              .getRecordsForMonth(emp.id, year, month);
            }

            // Skip employees with no records in this period
            if (records.isEmpty()) continue;

            if (allTime) {
                // ── All-time CSV header ───────────────────────────────────
                // Write a fully import-compatible header block.
                // Period = "All Time" — the importer skips unknown period
                // values and determines pay type from the wage rows below.
                out << "Rawatib - Monthly Attendance Report" << '\n';
                out << "Employee" << "," << csvField(emp.name) << '\n';
                out << "Period" << "," << "All Time" << '\n';
                if (wagesVisible) {
                    if (emp.isMonthly()) {
                        out << "Monthly Salary" << ","
                            << csvField(CurrencyManager::format(emp.monthlySalary)) << '\n';
                        out << "Monthly Salary Raw" << ","
                            << QString::number(emp.monthlySalary, 'f', 2) << '\n';
                        out << "Working Days/Month" << ","
                            << QString::number(emp.workingDaysPerMonth) << '\n';
                        if (emp.expectedCheckin.isValid())
                            out << "Expected Check-In" << ","
                                << emp.expectedCheckin.toString("HH:mm") << '\n';
                        if (emp.expectedCheckout.isValid())
                            out << "Expected Check-Out" << ","
                                << emp.expectedCheckout.toString("HH:mm") << '\n';
                    } else {
                        out << "Hourly Wage" << ","
                            << csvField(CurrencyManager::format(emp.hourlyWage)) << '\n';
                        out << "Hourly Wage Raw" << ","
                            << QString::number(emp.hourlyWage, 'f', 2) << '\n';
                    }
                }
                out << "Exported" << ","
                    << QDate::currentDate().toString("yyyy-MM-dd") << '\n';
                out << '\n';

                // Column headers
                if (emp.isMonthly()) {
                    writeCsvRow(out, {
                        "Date", "Check-In", "Check-Out", "Hours",
                        "Net Day", "Status", "Base Rate", "Deduction"
                    });
                } else {
                    writeCsvRow(out, {
                        "Date", "Check-In", "Check-Out", "Hours",
                        "Daily Wage", "Status"
                    });
                }

                // Data rows
                for (const auto& r : records) {
                    if (r.isOpen()) {
                        if (emp.isMonthly()) {
                            writeCsvRow(out, {
                                r.date.toString("yyyy-MM-dd"),
                                r.checkIn.toString("HH:mm"),
                                "--", "--", "--", "Open", "--", "--"
                            });
                        } else {
                            writeCsvRow(out, {
                                r.date.toString("yyyy-MM-dd"),
                                r.checkIn.toString("HH:mm"),
                                "--", "--", "--", "Open"
                            });
                        }
                    } else {
                        if (emp.isMonthly()) {
                            writeCsvRow(out, {
                                r.date.toString("yyyy-MM-dd"),
                                r.checkIn.toString("HH:mm"),
                                r.checkOut.toString("HH:mm"),
                                QString::number(r.hoursWorked, 'f', 2),
                                wagesVisible ? QString::number(r.dailyWage,     'f', 2) : QStringLiteral("--"),
                                r.paid ? "Paid" : "Unpaid",
                                wagesVisible ? QString::number(r.baseDailyRate, 'f', 2) : QStringLiteral("--"),
                                wagesVisible ? QString::number(r.dayDeduction,  'f', 2) : QStringLiteral("--")
                            });
                        } else {
                            writeCsvRow(out, {
                                r.date.toString("yyyy-MM-dd"),
                                r.checkIn.toString("HH:mm"),
                                r.checkOut.toString("HH:mm"),
                                QString::number(r.hoursWorked, 'f', 2),
                                wagesVisible ? QString::number(r.dailyWage, 'f', 2) : QStringLiteral("--"),
                                r.paid ? "Paid" : "Unpaid"
                            });
                        }
                    }
                }

                // Summary checksum block (matches ImportHelper expectations)
                double totalHours  = 0.0;
                double totalSalary = 0.0;
                for (const auto& r : records) {
                    if (!r.isOpen()) {
                        totalHours  += r.hoursWorked;
                        totalSalary += r.dailyWage;
                    }
                }
                out << '\n';
                out << "Summary" << '\n';
                writeCsvRow(out, { "Total Days",  QString::number(records.size()) });
                writeCsvRow(out, { "Total Hours", QString::number(totalHours,  'f', 2) });
                if (wagesVisible)
                    writeCsvRow(out, { "Total Salary", QString::number(totalSalary, 'f', 2) });
                out << '\n';

            } else {
                // ── Monthly export — delegate to battle-tested function ───
                const auto summary = AttendanceRepository::instance()
                                         .getMonthlySummary(emp.id, year, month);
                writeCsvEmployeeBlock(out, emp.name, month, year, emp,
                                      records, summary,
                                      PayrollCalculator::Result{},
                                      wagesVisible);
            }

            ++employeesExported;
            recordsExported += records.size();
        }

        file.close();
        ok = true;

        if (employeesExported == 0) {
            QFile::remove(path);
            QMessageBox::information(parent,
                tr("Export All Employees"),
                allTime
                    ? tr("No records found for any employee.")
                    : tr("No records found for any employee in the selected period."));
            return;
        }

    } else {
#if QXLSX_AVAILABLE
        // ── XLSX path ─────────────────────────────────────────────────────
        auto* doc = new QXlsx::Document();

        // Remove the default empty sheet that QXlsx creates
        if (!doc->sheetNames().isEmpty())
            doc->deleteSheet(doc->sheetNames().first());

        for (const auto& emp : employees) {
            QVector<AttendanceRecord> records;
            if (allTime) {
                records = AttendanceRepository::instance()
                              .getRecordsForEmployee(emp.id);
            } else {
                records = AttendanceRepository::instance()
                              .getRecordsForMonth(emp.id, year, month);
            }

            if (records.isEmpty()) continue;

            // For all-time XLSX: pass the pre-fetched records directly so
            // writeEmployeeSheet doesn't re-fetch using a bogus year=0 query.
            // Pass month=0/year=0 as sentinel — writeEmployeeSheet shows
            // "All Records" as the period label when year == 0.
            const int sheetMonth = allTime ? 0 : month;
            const int sheetYear  = allTime ? 0 : year;

            writeEmployeeSheet(*doc,
                               safeSheetName(emp.name),
                               emp.name,
                               sheetMonth, sheetYear,
                               emp,
                               emp.id,
                               PayrollCalculator::Result{},
                               wagesVisible,
                               allTime ? records : QVector<AttendanceRecord>{});

            ++employeesExported;
            recordsExported += records.size();
        }

        if (employeesExported == 0) {
            delete doc;
            QMessageBox::information(parent,
                tr("Export All Employees"),
                allTime
                    ? tr("No records found for any employee.")
                    : tr("No records found for any employee in the selected period."));
            return;
        }

        ok = doc->saveAs(path);
        delete doc;
#else
        QMessageBox::warning(parent, tr("Not Available"),
            tr("XLSX export requires QXlsx.\n"
               "See third_party/QXlsx/README.md for setup instructions."));
        return;
#endif
    }

    // ── Step 5: Success dialog ────────────────────────────────────────────
    if (ok) {
        QMessageBox msgBox(parent);
        msgBox.setWindowTitle(tr("Export Successful"));
        msgBox.setText(
            tr("Exported %1 record(s) for %2 employee(s) to:\n%3")
                .arg(recordsExported)
                .arg(employeesExported)
                .arg(path));
        msgBox.setIcon(QMessageBox::Information);

        QPushButton* openBtn = msgBox.addButton(
            tr("Open Folder"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();

        if (msgBox.clickedButton() == openBtn) {
#if defined(Q_OS_WIN)
            QProcess::startDetached(
                "explorer.exe",
                { "/select,", QDir::toNativeSeparators(path) });
#elif defined(Q_OS_MACOS)
            QProcess::startDetached("open", { "-R", path });
#else
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
        }
    } else {
        QMessageBox::critical(parent, tr("Export Failed"),
            tr("Could not write file to:\n%1\n\n"
               "Check that the folder is writable.").arg(path));
    }
}

} // namespace ExportHelper
