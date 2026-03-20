#pragma once
#include "models/PayrollRule.h"
#include "models/Employee.h"
#include <QVector>
#include <QString>

// ── PayrollRulesRepository ─────────────────────────────────────────────────
//
// CRUD access for the payroll_rules table.
// Follows the same singleton + lastError() pattern as AttendanceRepository.

class PayrollRulesRepository {
public:
    static PayrollRulesRepository& instance();

    // Returns all rules ordered by sort_order ASC, id ASC.
    QVector<PayrollRule> getAllRules() const;

    // Returns all rules for a specific applies_to value.
    QVector<PayrollRule> getRulesForTab(PayrollRule::AppliesTo tab) const;

    // Returns only enabled rules for a given pay type —
    // includes AppliesTo::All rules plus rules matching the pay type.
    QVector<PayrollRule> getEnabledRulesForPayType(PayType type) const;

    // Returns all enabled rules regardless of applies_to — legacy/fallback.
    QVector<PayrollRule> getEnabledRules() const;

    bool addRule(PayrollRule& rule);       // sets rule.id on success
    bool updateRule(const PayrollRule& rule);
    bool deleteRule(int id);

    QString lastError() const;

private:
    PayrollRulesRepository() = default;
    mutable QString m_lastError;
};
