#include "utils/PayrollCalculator.h"
#include <cmath>

namespace PayrollCalculator {

QVector<AppliedRule> buildAppliedRules(const QVector<PayrollRule>& enabledRules) {
    QVector<AppliedRule> result;
    result.reserve(enabledRules.size());
    for (const auto& r : enabledRules)
        result.append(AppliedRule(r));
    return result;
}

Result calculate(double grossPay, const QVector<AppliedRule>& appliedRules) {
    Result res;
    res.grossPay = grossPay;

    for (const auto& ar : appliedRules) {
        if (!ar.rule.enabled) continue;

        // Resolve the actual monetary amount for this rule
        double amount = 0.0;
        if (ar.rule.basis == PayrollRule::Basis::FixedAmount) {
            amount = ar.appliedValue;
        } else {
            // PercentOfGross — clamp to [0,100] defensively
            const double pct = std::clamp(ar.appliedValue, 0.0, 100.0);
            amount = grossPay * pct / 100.0;
        }

        // Round to 2 decimal places to avoid floating-point noise in display
        amount = std::round(amount * 100.0) / 100.0;

        if (ar.rule.type == PayrollRule::Type::Deduction) {
            res.totalDeductions += amount;
            res.breakdown.append({ ar.rule.name, -amount });
        } else {
            res.totalAdditions += amount;
            res.breakdown.append({ ar.rule.name, +amount });
        }
    }

    res.netPay = grossPay + res.totalAdditions - res.totalDeductions;

    // Clamp net to zero — can't pay negative
    if (res.netPay < 0.0) res.netPay = 0.0;

    return res;
}

} // namespace PayrollCalculator
