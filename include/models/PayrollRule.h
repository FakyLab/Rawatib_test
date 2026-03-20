#pragma once
#include <QString>

// ── PayrollRule ────────────────────────────────────────────────────────────
//
// Represents one payroll rule (deduction or addition).
// Rules are stored in the payroll_rules table.
//
// type:   Deduction — subtracted from gross pay
//         Addition  — added to gross pay
//
// basis:  FixedAmount    — value is a flat currency amount
//         PercentOfGross — value is a percentage of gross pay (0–100)
//
// enabled: quick on/off per rule without deleting it.
//
// sortOrder: display order in the UI table.
//
// appliesTo: which employee pay type this rule applies to.
//   All     — applies to every employee regardless of pay type (default)
//   Monthly — applies to monthly salary employees only
//   Hourly  — applies to hourly employees only

struct PayrollRule {
    int     id        = 0;
    QString name;

    enum class Type      : int { Deduction = 0, Addition  = 1 };
    enum class Basis     : int { FixedAmount = 0, PercentOfGross = 1 };
    enum class AppliesTo : int { All = 0, Monthly = 1, Hourly = 2 };

    Type      type      = Type::Deduction;
    Basis     basis     = Basis::FixedAmount;
    double    value     = 0.0;
    bool      enabled   = true;
    int       sortOrder = 0;
    AppliesTo appliesTo = AppliesTo::All;

    bool isValid() const {
        return !name.trimmed().isEmpty() && value >= 0.0;
    }
};
