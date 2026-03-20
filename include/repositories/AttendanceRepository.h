#pragma once
#include "models/AttendanceRecord.h"
#include "models/Employee.h"
#include <QVector>
#include <QString>
#include <QDate>

struct MonthlySummary {
    int    totalDays    = 0;
    double totalHours   = 0.0;
    double totalSalary  = 0.0;   // SUM(daily_wage) — net days earned, already includes late/early deductions
    double paidAmount   = 0.0;
    double unpaidAmount = 0.0;

    // Monthly salary employees only — computed alongside totalSalary
    bool   isMonthly           = false;
    double expectedSalary      = 0.0;
    int    presentDays         = 0;     // days with a completed record
    int    exceptionDays       = 0;     // approved off-days — not counted as absent
    int    absentDays          = 0;     // workingDaysPerMonth - presentDays - exceptionDays
    int    totalLateMinutes    = 0;
    int    totalEarlyMinutes   = 0;

    // ── Deduction breakdown (monthly only) ───────────────────────────────
    // totalDayDeduction: SUM(day_deduction) from attendance records —
    //   the total late+early monetary deductions already baked into totalSalary.
    //   Used for display breakdown only — NOT added to totalDeductions again.
    double totalDayDeduction   = 0.0;
    double absentDeduction     = 0.0;   // absentDays × dailyRate
    double lateDeduction       = 0.0;   // totalLateMinutes × perMinuteRate (display only)
    double earlyDeduction      = 0.0;   // totalEarlyMinutes × perMinuteRate (display only)

    // totalDeductions = absentDeduction only.
    // late+early are already reflected in totalSalary via per-record day_deduction.
    double totalDeductions     = 0.0;

    // True when this month contains records calculated under different deduction
    // modes — e.g. some under PerMinute and some under PerDay after a mid-month
    // mode change. Used to show a warning banner in the UI.
    bool   hasMixedModes       = false;
};

class AttendanceRepository {
public:
    static AttendanceRepository& instance();

    bool addRecord(AttendanceRecord& record);
    bool updateRecord(const AttendanceRecord& record);
    bool deleteRecord(int id);
    bool recordExistsForDate(int employeeId, const QDate& date) const;

    // Kiosk check-in / check-out
    bool checkIn(int employeeId, const QDate& date, const QTime& time);
    bool checkOut(int recordId, const QTime& time, const Employee& employee);
    // Returns the most recent open record for the given employee+date (id==0 if none)
    AttendanceRecord getTodayRecord(int employeeId, const QDate& date) const;
    // Returns count of records for a given employee+date
    int recordCountForDate(int employeeId, const QDate& date) const;

    QVector<AttendanceRecord> getRecordsForEmployee(int employeeId) const;
    QVector<AttendanceRecord> getRecordsForMonth(int employeeId, int year, int month) const;

    // Returns the year of the oldest record in the DB, or curYear if no records exist.
    int getEarliestRecordYear() const;
    bool markPaid(int recordId);
    bool markPaidMultiple(const QVector<int>& ids);
    bool markMonthPaid(int employeeId, int year, int month);
    bool markUnpaid(int recordId);
    bool markUnpaidMultiple(const QVector<int>& ids);
    MonthlySummary getMonthlySummary(int employeeId, int year, int month) const;
    QString lastError() const;

private:
    AttendanceRepository() = default;
    mutable QString m_lastError;
};
