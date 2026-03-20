#pragma once
#include <QString>
#include <QTime>

// ── PayType ───────────────────────────────────────────────────────────────
// 0 = Hourly   — pay = hours_worked × hourly_wage
// 1 = Monthly  — fixed monthly salary with proportional deductions for
//                lateness, early departure, and absence
enum class PayType : int { Hourly = 0, Monthly = 1 };

struct Employee {
    int     id          = 0;
    QString name;
    QString phone;
    QString notes;

    // ── Hourly fields ─────────────────────────────────────────────────────
    PayType payType     = PayType::Hourly;
    double  hourlyWage  = 0.0;

    // ── Monthly salary fields (only used when payType == Monthly) ─────────
    double  monthlySalary        = 0.0;
    int     workingDaysPerMonth  = 26;      // denominator for daily rate
    QTime   expectedCheckin;               // e.g. 09:00 — invalid = not set
    QTime   expectedCheckout;              // e.g. 17:00 — invalid = not set
    int     lateToleranceMin     = 15;     // grace period before deducting

    // ── PIN (both pay types) ──────────────────────────────────────────────
    // Stored as PBKDF2-SHA256 hash + hex salt.
    // Both empty means no PIN set for this employee.
    QString pinHash;
    QString pinSalt;

    // ── Helpers ───────────────────────────────────────────────────────────
    bool isMonthly() const { return payType == PayType::Monthly; }

    // Daily base rate for a fully-present monthly employee
    double dailyRate() const {
        if (!isMonthly() || workingDaysPerMonth <= 0) return 0.0;
        return monthlySalary / workingDaysPerMonth;
    }

    // Per-minute rate used for deduction calculations
    double perMinuteRate() const {
        const double hoursPerDay = expectedCheckin.isValid() && expectedCheckout.isValid()
            ? expectedCheckin.secsTo(expectedCheckout) / 3600.0
            : 8.0;   // fallback: 8-hour workday
        if (hoursPerDay <= 0) return 0.0;
        return dailyRate() / (hoursPerDay * 60.0);
    }

    bool isValid() const { return !name.trimmed().isEmpty(); }
    bool hasPinSet() const { return !pinHash.isEmpty() && !pinSalt.isEmpty(); }
};
