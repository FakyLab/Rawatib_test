#pragma once
#include <QDate>
#include <QString>
#include <QVector>

// ── DayException ──────────────────────────────────────────────────────────
// A single day exception — a date on which an employee (or all employees)
// should not be counted as absent even if they have no attendance record.
//
// employee_id == 0 means company-wide (applies to every employee).
// employee_id >  0 means individual leave for that specific employee only.

struct DayException {
    int     id          = 0;
    QDate   date;
    int     employeeId  = 0;    // 0 = company-wide
    QString reason;             // free-text label, e.g. "Eid Al-Fitr"
};

class DayExceptionRepository {
public:
    static DayExceptionRepository& instance();

    bool addException(DayException& ex);
    bool removeException(int id);

    // All exceptions — for the management UI
    QVector<DayException> getAll() const;

    // Count exception days in [startDate, endDate] that apply to employeeId.
    // A date applies if employee_id IS NULL/0 (company-wide) OR == employeeId.
    // Each calendar date is counted at most once regardless of how many
    // matching rows exist.
    int countExceptionDays(int employeeId,
                           const QDate& startDate,
                           const QDate& endDate) const;

    QString lastError() const;

private:
    DayExceptionRepository() = default;
    mutable QString m_lastError;
};
