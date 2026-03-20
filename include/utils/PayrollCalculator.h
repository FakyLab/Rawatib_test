#pragma once
#include "models/PayrollRule.h"
#include <QString>
#include <QVector>

// ── PayrollCalculator ──────────────────────────────────────────────────────
//
// Pure stateless calculation — no DB access, no Qt widgets.
// Feed it a gross pay amount and a list of rules; get back a full breakdown.
//
// AppliedRule wraps a PayrollRule with an overridable value.
// In the Salary Summary tab, the user can edit the value of any AppliedRule
// for the current session — the global default in PayrollRule is unchanged.

struct AppliedRule {
    PayrollRule rule;
    double      appliedValue;   // starts as rule.value; user may override

    explicit AppliedRule(const PayrollRule& r)
        : rule(r), appliedValue(r.value) {}
};

namespace PayrollCalculator {

struct LineItem {
    QString name;
    double  amount;    // positive = addition to net, negative = deduction from net
};

struct Result {
    double              grossPay        = 0.0;
    double              totalAdditions  = 0.0;
    double              totalDeductions = 0.0;
    double              netPay          = 0.0;
    QVector<LineItem>   breakdown;      // one entry per enabled applied rule
};

// Build a list of AppliedRules from global enabled rules — call once per
// employee/month load, then let the user edit appliedValue in the UI.
QVector<AppliedRule> buildAppliedRules(const QVector<PayrollRule>& enabledRules);

// Run the calculation.
// appliedRules may have user-overridden values — they are used as-is.
Result calculate(double grossPay, const QVector<AppliedRule>& appliedRules);

} // namespace PayrollCalculator
