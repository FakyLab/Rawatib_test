#include "repositories/AttendanceRepository.h"
#include "repositories/EmployeeRepository.h"
#include "repositories/DayExceptionRepository.h"
#include "utils/AuditLog.h"
#include "utils/DeductionPolicy.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QCoreApplication>

// Translation shorthand — context matches <context><name> in rawatib_ar.ts
static inline QString tr(const char* key) {
    return QCoreApplication::translate("AttendanceRepository", key);
}

// Helper to get employee name for audit log detail strings
static QString empName(int employeeId) {
    auto emp = EmployeeRepository::instance().getEmployee(employeeId);
    return emp ? emp->name : QString::number(employeeId);
}

AttendanceRepository& AttendanceRepository::instance() {
    static AttendanceRepository inst;
    return inst;
}

QString AttendanceRepository::lastError() const {
    return m_lastError;
}

static AttendanceRecord fromQuery(QSqlQuery& q) {
    AttendanceRecord r;
    r.id         = q.value(0).toInt();
    r.employeeId = q.value(1).toInt();
    r.date       = QDate::fromString(q.value(2).toString(), Qt::ISODate);
    r.checkIn    = QTime::fromString(q.value(3).toString(), "HH:mm");
    // check_out is nullable — isNull() means open record
    r.checkOut   = q.value(4).isNull()
                       ? QTime()   // invalid = open
                       : QTime::fromString(q.value(4).toString(), "HH:mm");
    r.hoursWorked            = q.value(5).isNull() ? 0.0 : q.value(5).toDouble();
    r.baseDailyRate          = q.value(6).toDouble();
    r.dayDeduction           = q.value(7).toDouble();
    r.dailyWage              = q.value(8).isNull() ? 0.0 : q.value(8).toDouble();
    r.paid                   = q.value(9).toBool();
    r.lateMinutes            = q.value(10).toInt();
    r.earlyMinutes           = q.value(11).toInt();
    r.appliedDeductionMode   = q.value(12).toString();
    return r;
}

// ── #1 Duplicate date check ────────────────────────────────────────────────
bool AttendanceRepository::recordExistsForDate(int employeeId, const QDate& date) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM attendance WHERE employee_id=:emp AND date=:date");
    q.bindValue(":emp",  employeeId);
    q.bindValue(":date", date.toString(Qt::ISODate));
    if (q.exec() && q.next())
        return q.value(0).toInt() > 0;
    return false;
}

// ── Overlap detection ─────────────────────────────────────────────────────
//
// Returns an empty QString if no overlap, or a human-readable error message
// describing the conflicting session if one is found.
//
// excludeId: pass the record's own id when editing so it doesn't conflict
//            with itself. Pass 0 when adding a new record.
//
// Rules:
//   Completed vs completed: overlap if A_in < B_out AND B_in < A_out (strict)
//   New record vs open record: block if new check-in >= open check-in,
//     because the open session's end is unknown so we can't verify safety.
//   New record starts BEFORE open check-in: allowed (back-dating an earlier slot).

static QString checkOverlap(int employeeId, const QDate& date,
                             const QTime& newIn, const QTime& newOut,
                             bool newIsOpen, int excludeId)
{
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare(R"(SELECT id, check_in, check_out
                 FROM attendance
                 WHERE employee_id=:emp AND date=:date AND id!=:excl
                 ORDER BY check_in ASC)");
    q.bindValue(":emp",  employeeId);
    q.bindValue(":date", date.toString(Qt::ISODate));
    q.bindValue(":excl", excludeId);
    if (!q.exec()) return QString();   // DB error — let the caller deal with it

    while (q.next()) {
        const QTime existIn  = QTime::fromString(q.value(1).toString(), "HH:mm");
        const bool  existOpen = q.value(2).isNull();
        const QTime existOut = existOpen
                                   ? QTime()
                                   : QTime::fromString(q.value(2).toString(), "HH:mm");

        if (existOpen) {
            // Existing open session — unknown end time.
            // Block if new record starts at or after its check-in.
            if (newIn >= existIn) {
                return tr(
                    "An open session exists from %1 on this date. "
                    "Close it before adding a session that starts after it.")
                    .arg(existIn.toString("hh:mm AP"));
            }
            // New record starts before open session — allowed.
            continue;
        }

        // Both are completed. Standard interval overlap: A_in < B_out AND B_in < A_out
        // (strict inequalities — touching end-to-start is fine)
        if (newIsOpen) {
            // New record is open — its end is unknown.
            // Block if it starts before an existing session ends
            // (new open session would swallow anything starting after it).
            if (newIn < existOut && existIn < newIn) {
                // new check-in falls inside an existing session
                return tr(
                    "This session starts at %1 which falls inside an existing "
                    "session (%2 – %3).")
                    .arg(newIn.toString("hh:mm AP"),
                         existIn.toString("hh:mm AP"),
                         existOut.toString("hh:mm AP"));
            }
            // Also block if a completed session starts inside the new open session
            if (existIn >= newIn) {
                return tr(
                    "An open session starting at %1 would conflict with an "
                    "existing session (%2 – %3). Close or adjust the existing "
                    "session first.")
                    .arg(newIn.toString("hh:mm AP"),
                         existIn.toString("hh:mm AP"),
                         existOut.toString("hh:mm AP"));
            }
            continue;
        }

        // Both completed — standard overlap test
        if (newIn < existOut && existIn < newOut) {
            return tr(
                "This session (%1 – %2) overlaps with an existing session (%3 – %4).")
                .arg(newIn.toString("hh:mm AP"),
                     newOut.toString("hh:mm AP"),
                     existIn.toString("hh:mm AP"),
                     existOut.toString("hh:mm AP"));
        }
    }
    return QString();   // no overlap
}

bool AttendanceRepository::addRecord(AttendanceRecord& record) {
    // ── Overlap guard ──────────────────────────────────────────────────────
    const QString overlapErr = checkOverlap(
        record.employeeId, record.date,
        record.checkIn, record.checkOut,
        record.isOpen(), /*excludeId=*/0);
    if (!overlapErr.isEmpty()) {
        m_lastError = overlapErr;
        return false;
    }

    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO attendance
                   (employee_id, date, check_in, check_out, hours,
                    base_daily_rate, day_deduction, daily_wage,
                    paid_status, late_minutes, early_minutes, applied_deduction_mode)
                 VALUES (:emp, :date, :ci, :co, :hrs, :base, :ded, :wage, :paid, :late, :early, :mode))");
    q.bindValue(":emp",  record.employeeId);
    q.bindValue(":date", record.date.toString(Qt::ISODate));
    q.bindValue(":ci",   record.checkIn.toString("HH:mm"));
    // Allow NULL check_out for open records
    if (record.checkOut.isValid())
        q.bindValue(":co",   record.checkOut.toString("HH:mm"));
    else
        q.bindValue(":co",   QVariant(QMetaType(QMetaType::QString)));
    // Allow NULL hours/wage for open records
    if (record.isOpen()) {
        q.bindValue(":hrs",  QVariant(QMetaType(QMetaType::Double)));
        q.bindValue(":base", 0.0);
        q.bindValue(":ded",  0.0);
        q.bindValue(":wage", QVariant(QMetaType(QMetaType::Double)));
        q.bindValue(":mode", QVariant(QMetaType(QMetaType::QString)));  // NULL for open
    } else {
        q.bindValue(":hrs",  record.hoursWorked);
        q.bindValue(":base", record.baseDailyRate);
        q.bindValue(":ded",  record.dayDeduction);
        q.bindValue(":wage", record.dailyWage);
        // NULL for hourly (empty string), mode string for monthly
        q.bindValue(":mode", record.appliedDeductionMode.isEmpty()
                                 ? QVariant(QMetaType(QMetaType::QString))
                                 : QVariant(record.appliedDeductionMode));
    }
    q.bindValue(":paid",  record.paid ? 1 : 0);
    q.bindValue(":late",  record.lateMinutes);
    q.bindValue(":early", record.earlyMinutes);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "addRecord failed:" << m_lastError;
        return false;
    }
    record.id = q.lastInsertId().toInt();
    AuditLog::record(AuditLog::ADD_ATTENDANCE, "attendance", record.id,
        QString("%1 %2 %3-%4").arg(empName(record.employeeId))
            .arg(record.date.toString("yyyy-MM-dd"))
            .arg(record.checkIn.toString("HH:mm"))
            .arg(record.isOpen() ? "open" : record.checkOut.toString("HH:mm")));
    return true;
}

// ── Kiosk: check employee in for today ────────────────────────────────────
bool AttendanceRepository::checkIn(int employeeId, const QDate& date, const QTime& time) {
    // Guard: refuse if there is already an open (unclosed) record for today.
    auto open = getTodayRecord(employeeId, date);
    if (open.id != 0 && open.isOpen()) {
        m_lastError = QString("Employee already has an open check-in for %1.")
                          .arg(date.toString("yyyy-MM-dd"));
        return false;
    }

    // Guard: refuse if the new check-in time overlaps any existing session.
    // checkOut is invalid (open record) — pass QTime() for newOut.
    const QString overlapErr = checkOverlap(
        employeeId, date, time, QTime(), /*newIsOpen=*/true, /*excludeId=*/0);
    if (!overlapErr.isEmpty()) {
        m_lastError = overlapErr;
        return false;
    }

    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO attendance (employee_id, date, check_in, check_out, hours, daily_wage, paid_status)
                 VALUES (:emp, :date, :ci, NULL, NULL, NULL, 0))");
    q.bindValue(":emp",  employeeId);
    q.bindValue(":date", date.toString(Qt::ISODate));
    q.bindValue(":ci",   time.toString("HH:mm"));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "checkIn failed:" << m_lastError;
        return false;
    }
    return true;
}

// ── Kiosk: check employee out, calculate hours and wage ───────────────────
bool AttendanceRepository::checkOut(int recordId, const QTime& time, const Employee& employee) {
    auto& db = DatabaseManager::instance().database();

    // Fetch the check-in time and employee_id for this record
    QSqlQuery fetch(db);
    fetch.prepare("SELECT check_in, employee_id, date FROM attendance WHERE id=:id");
    fetch.bindValue(":id", recordId);
    if (!fetch.exec() || !fetch.next()) {
        m_lastError = "Record not found for check-out.";
        return false;
    }
    const QTime checkIn = QTime::fromString(fetch.value(0).toString(), "HH:mm");
    const int   empId   = fetch.value(1).toInt();
    const QDate rDate   = QDate::fromString(fetch.value(2).toString(), Qt::ISODate);

    if (!checkIn.isValid() || time <= checkIn) {
        m_lastError = "Check-out time must be after check-in time.";
        return false;
    }

    // Overlap guard: full range now known
    const QString overlapErr = checkOverlap(
        empId, rDate, checkIn, time, /*newIsOpen=*/false, /*excludeId=*/recordId);
    if (!overlapErr.isEmpty()) {
        m_lastError = overlapErr;
        return false;
    }

    // Calculate wage depending on pay type
    AttendanceRecord r;
    r.checkIn  = checkIn;
    r.checkOut = time;

    if (employee.isMonthly()) {
        r.calculateMonthly(employee.expectedCheckin, employee.expectedCheckout,
                           employee.lateToleranceMin,
                           employee.dailyRate(), employee.perMinuteRate(),
                           DeductionPolicy::mode(),
                           DeductionPolicy::perDayPenaltyPct());
    } else {
        r.calculate(employee.hourlyWage);
    }

    QSqlQuery q(db);
    q.prepare(R"(UPDATE attendance
                 SET check_out=:co, hours=:hrs,
                     base_daily_rate=:base, day_deduction=:ded, daily_wage=:wage,
                     late_minutes=:late, early_minutes=:early,
                     applied_deduction_mode=:mode
                 WHERE id=:id)");
    q.bindValue(":co",    time.toString("HH:mm"));
    q.bindValue(":hrs",   r.hoursWorked);
    q.bindValue(":base",  r.baseDailyRate);
    q.bindValue(":ded",   r.dayDeduction);
    q.bindValue(":wage",  r.dailyWage);
    q.bindValue(":late",  r.lateMinutes);
    q.bindValue(":early", r.earlyMinutes);
    q.bindValue(":mode",  r.appliedDeductionMode.isEmpty()
                              ? QVariant(QMetaType(QMetaType::QString))
                              : QVariant(r.appliedDeductionMode));
    q.bindValue(":id",    recordId);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "checkOut failed:" << m_lastError;
        return false;
    }
    return true;
}

// ── Get the open record for a specific employee+date ──────────────────────
// Returns the most recent open (no check-out) record, or id==0 if none.
AttendanceRecord AttendanceRepository::getTodayRecord(int employeeId, const QDate& date) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    // Prefer the open record if one exists; order by id DESC to get most recent
    q.prepare(R"(SELECT id, employee_id, date, check_in, check_out, hours,
                        base_daily_rate, day_deduction, daily_wage, paid_status,
                        late_minutes, early_minutes, applied_deduction_mode
                 FROM attendance
                 WHERE employee_id=:emp AND date=:date AND check_out IS NULL
                 ORDER BY id DESC LIMIT 1)");
    q.bindValue(":emp",  employeeId);
    q.bindValue(":date", date.toString(Qt::ISODate));
    if (q.exec() && q.next())
        return fromQuery(q);
    return AttendanceRecord{};  // id==0 means no open record
}

// ── Count records for a specific employee+date ────────────────────────────
int AttendanceRepository::recordCountForDate(int employeeId, const QDate& date) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM attendance WHERE employee_id=:emp AND date=:date");
    q.bindValue(":emp",  employeeId);
    q.bindValue(":date", date.toString(Qt::ISODate));
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

bool AttendanceRepository::updateRecord(const AttendanceRecord& record) {
    // ── Overlap guard (exclude this record from the check) ─────────────────
    const QString overlapErr = checkOverlap(
        record.employeeId, record.date,
        record.checkIn, record.checkOut,
        record.isOpen(), /*excludeId=*/record.id);
    if (!overlapErr.isEmpty()) {
        m_lastError = overlapErr;
        return false;
    }

    auto& db = DatabaseManager::instance().database();

    QSqlQuery q(db);
    q.prepare(R"(UPDATE attendance SET date=:date, check_in=:ci, check_out=:co,
                 hours=:hrs, base_daily_rate=:base, day_deduction=:ded,
                 daily_wage=:wage, paid_status=:paid,
                 late_minutes=:late, early_minutes=:early,
                 applied_deduction_mode=:mode WHERE id=:id)");
    q.bindValue(":date", record.date.toString(Qt::ISODate));
    q.bindValue(":ci",   record.checkIn.toString("HH:mm"));
    if (record.checkOut.isValid())
        q.bindValue(":co",   record.checkOut.toString("HH:mm"));
    else
        q.bindValue(":co",   QVariant(QMetaType(QMetaType::QString)));
    if (record.isOpen()) {
        q.bindValue(":hrs",  QVariant(QMetaType(QMetaType::Double)));
        q.bindValue(":base", 0.0);
        q.bindValue(":ded",  0.0);
        q.bindValue(":wage", QVariant(QMetaType(QMetaType::Double)));
        q.bindValue(":mode", QVariant(QMetaType(QMetaType::QString)));  // NULL for open
    } else {
        q.bindValue(":hrs",  record.hoursWorked);
        q.bindValue(":base", record.baseDailyRate);
        q.bindValue(":ded",  record.dayDeduction);
        q.bindValue(":wage", record.dailyWage);
        q.bindValue(":mode", record.appliedDeductionMode.isEmpty()
                                 ? QVariant(QMetaType(QMetaType::QString))
                                 : QVariant(record.appliedDeductionMode));
    }
    q.bindValue(":paid",  record.paid ? 1 : 0);
    q.bindValue(":late",  record.lateMinutes);
    q.bindValue(":early", record.earlyMinutes);
    q.bindValue(":id",    record.id);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "updateRecord failed:" << m_lastError;
        return false;
    }
    AuditLog::record(AuditLog::EDIT_ATTENDANCE, "attendance", record.id,
        QString("%1 %2").arg(empName(record.employeeId)).arg(record.date.toString("yyyy-MM-dd")));
    return true;
}

bool AttendanceRepository::deleteRecord(int id) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("DELETE FROM attendance WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    AuditLog::record(AuditLog::DELETE_ATTENDANCE, "attendance", id,
        QString("Deleted record id=%1").arg(id));
    return true;
}

int AttendanceRepository::getEarliestRecordYear() const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    // Extract year from the ISO date string stored as TEXT (format: "YYYY-MM-DD")
    q.prepare("SELECT MIN(SUBSTR(date, 1, 4)) FROM attendance");
    if (q.exec() && q.next() && !q.value(0).isNull())
        return q.value(0).toInt();
    return QDate::currentDate().year();   // no records yet — use current year
}

QVector<AttendanceRecord> AttendanceRepository::getRecordsForEmployee(int employeeId) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT id, employee_id, date, check_in, check_out, hours,"
              "       base_daily_rate, day_deduction, daily_wage, paid_status,"
              "       late_minutes, early_minutes, applied_deduction_mode "
              "FROM attendance WHERE employee_id=:emp ORDER BY date DESC");
    q.bindValue(":emp", employeeId);
    QVector<AttendanceRecord> records;
    if (!q.exec()) { m_lastError = q.lastError().text(); return records; }
    while (q.next()) records.append(fromQuery(q));
    return records;
}

QVector<AttendanceRecord> AttendanceRepository::getRecordsForMonth(int employeeId, int year, int month) const {
    auto& db = DatabaseManager::instance().database();
    const QString startDate = QDate(year, month, 1).toString(Qt::ISODate);
    const QString endDate   = QDate(year, month, 1).addMonths(1).addDays(-1).toString(Qt::ISODate);
    QSqlQuery q(db);
    q.prepare(R"(SELECT id, employee_id, date, check_in, check_out, hours,
                        base_daily_rate, day_deduction, daily_wage, paid_status,
                        late_minutes, early_minutes, applied_deduction_mode
                 FROM attendance WHERE employee_id=:emp AND date BETWEEN :start AND :end
                 ORDER BY date ASC)");
    q.bindValue(":emp",   employeeId);
    q.bindValue(":start", startDate);
    q.bindValue(":end",   endDate);
    QVector<AttendanceRecord> records;
    if (!q.exec()) { m_lastError = q.lastError().text(); return records; }
    while (q.next()) records.append(fromQuery(q));
    return records;
}

// ── Mark Paid ──────────────────────────────────────────────────────────────
bool AttendanceRepository::markPaid(int recordId) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE attendance SET paid_status=1 WHERE id=:id");
    q.bindValue(":id", recordId);
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    AuditLog::record(AuditLog::MARK_PAID, "attendance", recordId,
        QString("Record id=%1 marked paid").arg(recordId));
    return true;
}

bool AttendanceRepository::markPaidMultiple(const QVector<int>& ids) {
    if (ids.isEmpty()) return true;
    auto& db = DatabaseManager::instance().database();
    db.transaction();
    for (int id : ids) {
        QSqlQuery q(db);
        q.prepare("UPDATE attendance SET paid_status=1 WHERE id=:id");
        q.bindValue(":id", id);
        if (!q.exec()) { m_lastError = q.lastError().text(); db.rollback(); return false; }
    }
    db.commit();
    // Audit log — one entry for the bulk operation
    AuditLog::record(AuditLog::MARK_PAID, "attendance", 0,
        QString("%1 record(s) marked paid: ids=%2")
            .arg(ids.size())
            .arg([&ids](){
                QStringList s; for (int id : ids) s << QString::number(id);
                return s.join(",");
            }()));
    return true;
}

bool AttendanceRepository::markMonthPaid(int employeeId, int year, int month) {
    auto& db = DatabaseManager::instance().database();
    const QString startDate = QDate(year, month, 1).toString(Qt::ISODate);
    const QString endDate   = QDate(year, month, 1).addMonths(1).addDays(-1).toString(Qt::ISODate);
    QSqlQuery q(db);
    q.prepare("UPDATE attendance SET paid_status=1 WHERE employee_id=:emp AND date BETWEEN :start AND :end");
    q.bindValue(":emp",   employeeId);
    q.bindValue(":start", startDate);
    q.bindValue(":end",   endDate);
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    AuditLog::record(AuditLog::PAY_MONTH, "employee", employeeId,
        QString("%1 %2/%3 all records marked paid").arg(empName(employeeId)).arg(year).arg(month));
    return true;
}

// ── #3 Mark Unpaid ────────────────────────────────────────────────────────
bool AttendanceRepository::markUnpaid(int recordId) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE attendance SET paid_status=0 WHERE id=:id");
    q.bindValue(":id", recordId);
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    AuditLog::record(AuditLog::MARK_UNPAID, "attendance", recordId,
        QString("Record id=%1 marked unpaid").arg(recordId));
    return true;
}

bool AttendanceRepository::markUnpaidMultiple(const QVector<int>& ids) {
    if (ids.isEmpty()) return true;
    auto& db = DatabaseManager::instance().database();
    db.transaction();
    for (int id : ids) {
        QSqlQuery q(db);
        q.prepare("UPDATE attendance SET paid_status=0 WHERE id=:id");
        q.bindValue(":id", id);
        if (!q.exec()) { m_lastError = q.lastError().text(); db.rollback(); return false; }
    }
    db.commit();
    // Audit log — one entry for the bulk operation
    AuditLog::record(AuditLog::MARK_UNPAID, "attendance", 0,
        QString("%1 record(s) marked unpaid: ids=%2")
            .arg(ids.size())
            .arg([&ids](){
                QStringList s; for (int id : ids) s << QString::number(id);
                return s.join(",");
            }()));
    return true;
}

// ── #4 Monthly summary with day count ─────────────────────────────────────
MonthlySummary AttendanceRepository::getMonthlySummary(int employeeId, int year, int month) const {
    auto& db = DatabaseManager::instance().database();
    const QString startDate = QDate(year, month, 1).toString(Qt::ISODate);
    const QString endDate   = QDate(year, month, 1).addMonths(1).addDays(-1).toString(Qt::ISODate);
    QSqlQuery q(db);
    q.prepare(R"(SELECT
                   COUNT(*)                                                      AS total_days,
                   SUM(hours)                                                    AS total_hours,
                   SUM(daily_wage)                                               AS total_salary,
                   SUM(CASE WHEN paid_status=1 THEN daily_wage ELSE 0 END)      AS paid_amount,
                   SUM(late_minutes)                                             AS total_late,
                   SUM(early_minutes)                                            AS total_early,
                   SUM(day_deduction)                                            AS total_day_deduction
                 FROM attendance
                 WHERE employee_id=:emp AND date BETWEEN :start AND :end)");
    q.bindValue(":emp",   employeeId);
    q.bindValue(":start", startDate);
    q.bindValue(":end",   endDate);

    MonthlySummary summary;
    if (q.exec() && q.next()) {
        summary.totalDays         = q.value(0).toInt();
        summary.totalHours        = q.value(1).toDouble();
        summary.totalSalary       = q.value(2).toDouble();
        summary.paidAmount        = q.value(3).toDouble();
        summary.unpaidAmount      = summary.totalSalary - summary.paidAmount;
        summary.totalLateMinutes  = q.value(4).toInt();
        summary.totalEarlyMinutes = q.value(5).toInt();
        // totalDayDeduction is the sum of per-record late+early monetary deductions.
        // Already baked into daily_wage — used here for display breakdown only,
        // NOT added again to totalDeductions.
        summary.totalDayDeduction = q.value(6).toDouble();
    }

    // For monthly employees: fetch employee config and compute absence stats
    auto emp = EmployeeRepository::instance().getEmployee(employeeId);
    if (emp && emp->isMonthly()) {
        summary.isMonthly      = true;
        summary.expectedSalary = emp->monthlySalary;

        // Count distinct working days with at least one completed record
        QSqlQuery dq(db);
        dq.prepare(R"(SELECT COUNT(DISTINCT date) FROM attendance
                      WHERE employee_id=:emp AND date BETWEEN :start AND :end
                        AND check_out IS NOT NULL)");
        dq.bindValue(":emp",   employeeId);
        dq.bindValue(":start", startDate);
        dq.bindValue(":end",   endDate);
        summary.presentDays = (dq.exec() && dq.next()) ? dq.value(0).toInt() : 0;

        // Count approved off-days (public holidays + individual leave).
        // These days are excluded from absence — employee is not penalised.
        const QDate monthStart = QDate(year, month, 1);
        const QDate monthEnd   = monthStart.addMonths(1).addDays(-1);
        summary.exceptionDays = DayExceptionRepository::instance()
                                    .countExceptionDays(employeeId, monthStart, monthEnd);

        const int expectedDays = emp->workingDaysPerMonth;
        summary.absentDays = qMax(0,
            expectedDays - summary.presentDays - summary.exceptionDays);

        // ── Deductions ────────────────────────────────────────────────────
        // absentDed: days with no attendance record at all.
        // lateDed / earlyDed: already baked into each record's daily_wage
        //   via day_deduction — summed here from DB for display breakdown.
        //   NOT recalculated from minutes × rate to avoid any double-counting.
        const double dailyRate = emp->dailyRate();
        summary.absentDeduction = summary.absentDays * dailyRate;

        // Split totalDayDeduction into late vs early portions for display.
        // How we split depends on the current deduction mode:
        //   PerMinute — split proportionally by minute counts
        //   PerDay    — each occurrence is penaltyPct% of dailyRate;
        //               split by counting late vs early days from records
        //   Off       — both are zero (dayDeduction was always 0)
        const DeductionPolicy::Mode dmode = DeductionPolicy::mode();

        if (dmode == DeductionPolicy::Mode::Off ||
            summary.totalDayDeduction <= 0.0) {
            summary.lateDeduction  = 0.0;
            summary.earlyDeduction = 0.0;

        } else if (dmode == DeductionPolicy::Mode::PerMinute) {
            // Proportional split by minute counts
            const double perMin = emp->perMinuteRate();
            summary.lateDeduction  = std::round(
                summary.totalLateMinutes  * perMin * 100.0) / 100.0;
            summary.earlyDeduction = std::round(
                summary.totalEarlyMinutes * perMin * 100.0) / 100.0;

        } else {
            // PerDay — count how many records had late > 0 and early > 0
            // to reconstruct the per-occurrence penalty amounts
            const double penaltyPct = DeductionPolicy::perDayPenaltyPct();
            const double penaltyAmt = std::round(
                dailyRate * penaltyPct / 100.0 * 100.0) / 100.0;

            // Query the count of records with late/early occurrences this month
            QSqlQuery countQ(db);
            countQ.prepare(R"(
                SELECT
                    SUM(CASE WHEN late_minutes  > 0 THEN 1 ELSE 0 END) AS late_days,
                    SUM(CASE WHEN early_minutes > 0 THEN 1 ELSE 0 END) AS early_days
                FROM attendance
                WHERE employee_id=:emp AND date BETWEEN :start AND :end
                  AND check_out IS NOT NULL)");
            countQ.bindValue(":emp",   employeeId);
            countQ.bindValue(":start", startDate);
            countQ.bindValue(":end",   endDate);
            if (countQ.exec() && countQ.next()) {
                summary.lateDeduction  = countQ.value(0).toInt() * penaltyAmt;
                summary.earlyDeduction = countQ.value(1).toInt() * penaltyAmt;
            }
        }

        // totalDeductions = absent only — late/early already in totalSalary
        summary.totalDeductions = summary.absentDeduction;

        // ── Mixed deduction modes detection ───────────────────────────────
        // If records in this month were calculated under different modes,
        // flag it so the UI can show a warning banner.
        // Detect mixed deduction modes — only meaningful for monthly employees.
        // NULL rows (hourly records) are excluded by the IN filter automatically.
        QSqlQuery modeQ(db);
        modeQ.prepare(R"(
            SELECT COUNT(DISTINCT applied_deduction_mode)
            FROM attendance
            WHERE employee_id=:emp AND date BETWEEN :start AND :end
              AND check_out IS NOT NULL
              AND applied_deduction_mode IN ('perminute', 'perday', 'off'))");
        modeQ.bindValue(":emp",   employeeId);
        modeQ.bindValue(":start", startDate);
        modeQ.bindValue(":end",   endDate);
        if (modeQ.exec() && modeQ.next())
            summary.hasMixedModes = modeQ.value(0).toInt() > 1;
    }

    return summary;
}
