#pragma once
#include <QString>
#include <QDate>
#include <QTime>
#include "utils/DeductionPolicy.h"

struct AttendanceRecord {
    int     id         = 0;
    int     employeeId = 0;
    QDate   date;
    QTime   checkIn;
    QTime   checkOut;           // invalid QTime = open record (no check-out yet)
    double  hoursWorked = 0.0;
    double  dailyWage   = 0.0;
    bool    paid        = false;

    // ── Monthly salary deduction fields ───────────────────────────────────
    // Stored so the tree can show them even if expected times change later.
    // Both are 0 for hourly employees and for on-time/full-day monthly records.
    int     lateMinutes  = 0;   // minutes after expected check-in (post-tolerance)
    int     earlyMinutes = 0;   // minutes before expected check-out

    // ── Monthly salary transparency fields ────────────────────────────────
    // baseDailyRate: the full daily rate at record-creation time (monthly only).
    //   Stored so history stays accurate even if salary changes later.
    //   Always 0.0 for hourly employees.
    // dayDeduction: monetary sum of late + early deductions for this record.
    //   baseDailyRate - dayDeduction = dailyWage (net day earned).
    //   Stored so the summary can SUM(day_deduction) without recalculating
    //   from minutes × rate, avoiding any double-counting risk.
    //   Always 0.0 for hourly employees.
    // appliedDeductionMode: the DeductionPolicy mode in effect when this
    //   record was calculated. Stored for audit transparency and to detect
    //   mixed-mode months. Empty string for hourly employees.
    double  baseDailyRate        = 0.0;
    double  dayDeduction         = 0.0;
    QString appliedDeductionMode;

    // Returns true if this record has no check-out yet
    bool isOpen() const {
        return checkIn.isValid() && !checkOut.isValid();
    }

    // ── Hourly calculation ────────────────────────────────────────────────
    // Calculate hours and wage — only valid when both times are set
    void calculate(double hourlyWage) {
        if (checkIn.isValid() && checkOut.isValid() && checkOut > checkIn) {
            int seconds = checkIn.secsTo(checkOut);
            hoursWorked = seconds / 3600.0;
            dailyWage   = hoursWorked * hourlyWage;
        } else {
            hoursWorked = 0.0;
            dailyWage   = 0.0;
        }
        lateMinutes          = 0;
        earlyMinutes         = 0;
        baseDailyRate        = 0.0;
        dayDeduction         = 0.0;
        appliedDeductionMode = QString();  // NULL in DB — hourly has no deduction mode
    }

    // ── Monthly salary calculation ────────────────────────────────────────
    // Computes dailyWage = dailyBaseRate - deductions (net day earned).
    // baseDailyRate is always stored as the full rate — never deflated.
    // dayDeduction holds the monetary sum of late + early penalties.
    //
    // Behaviour depends on DeductionPolicy::Mode:
    //   PerMinute — deduct (lateMin + earlyMin) × perMinuteRate
    //   PerDay    — deduct penaltyPct% of dailyBaseRate if lateMinutes > 0,
    //               same independently if earlyMinutes > 0
    //   Off       — record minutes for display, dayDeduction = 0 always
    //
    // expectedCheckin / expectedCheckout: employee's scheduled times.
    // toleranceMin: grace period — lateness within this is not deducted.
    //   In PerDay mode, also used as the threshold before penalty triggers.
    // dailyBaseRate: monthlySalary / workingDaysPerMonth
    // perMinRate: dailyBaseRate / (workHoursPerDay * 60)  — PerMinute only
    // mode / penaltyPct: from DeductionPolicy
    void calculateMonthly(const QTime&          expectedCheckin,
                          const QTime&          expectedCheckout,
                          int                   toleranceMin,
                          double                dailyBaseRate,
                          double                perMinRate,
                          DeductionPolicy::Mode mode       = DeductionPolicy::Mode::PerMinute,
                          double                penaltyPct = 50.0) {
        hoursWorked          = 0.0;
        lateMinutes          = 0;
        earlyMinutes         = 0;
        baseDailyRate        = dailyBaseRate;
        dayDeduction         = 0.0;
        appliedDeductionMode = DeductionPolicy::modeToString(mode);

        if (!checkIn.isValid()) {
            // Absent — full day deduction handled at summary level
            dailyWage = 0.0;
            return;
        }

        // Hours worked (informational display only — does not drive wage)
        if (checkOut.isValid() && checkOut > checkIn)
            hoursWorked = checkIn.secsTo(checkOut) / 3600.0;

        // ── Calculate raw late/early minutes ─────────────────────────────
        // Always computed regardless of mode — stored for display in all cases.
        if (expectedCheckin.isValid() && checkIn > expectedCheckin) {
            const int rawLate = expectedCheckin.secsTo(checkIn) / 60;
            lateMinutes = qMax(0, rawLate - toleranceMin);
        }
        if (expectedCheckout.isValid() && checkOut.isValid()
                && checkOut < expectedCheckout) {
            earlyMinutes = checkOut.secsTo(expectedCheckout) / 60;
        }

        // ── Apply deduction based on mode ─────────────────────────────────
        switch (mode) {

        case DeductionPolicy::Mode::PerMinute: {
            // Proportional: deduct for every minute late or early
            const double lateDed  = lateMinutes  * perMinRate;
            const double earlyDed = earlyMinutes * perMinRate;
            dayDeduction = std::round((lateDed + earlyDed) * 100.0) / 100.0;
            break;
        }

        case DeductionPolicy::Mode::PerDay: {
            // Fixed penalty per occurrence — triggered independently for
            // late arrival and early departure when minutes exceed tolerance.
            // lateMinutes already has tolerance subtracted (> 0 means exceeded).
            // earlyMinutes has no tolerance built in — any early departure triggers.
            const double penalty = std::round(
                dailyBaseRate * penaltyPct / 100.0 * 100.0) / 100.0;
            double ded = 0.0;
            if (lateMinutes  > 0) ded += penalty;
            if (earlyMinutes > 0) ded += penalty;
            dayDeduction = ded;
            break;
        }

        case DeductionPolicy::Mode::Off:
            // Minutes recorded above for display — no monetary deduction
            dayDeduction = 0.0;
            break;
        }

        // Net day = base rate minus this day's deductions, clamped to zero
        dailyWage = qMax(0.0, dailyBaseRate - dayDeduction);
    }

    // A complete valid record has both times set and check-out after check-in
    bool isComplete() const {
        return employeeId > 0 && date.isValid()
            && checkIn.isValid() && checkOut.isValid()
            && checkOut > checkIn;
    }
};
