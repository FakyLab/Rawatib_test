#include "repositories/PayrollRulesRepository.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

PayrollRulesRepository& PayrollRulesRepository::instance() {
    static PayrollRulesRepository inst;
    return inst;
}

QString PayrollRulesRepository::lastError() const {
    return m_lastError;
}

static PayrollRule fromQuery(QSqlQuery& q) {
    PayrollRule r;
    r.id        = q.value(0).toInt();
    r.name      = q.value(1).toString();
    r.type      = static_cast<PayrollRule::Type>     (q.value(2).toInt());
    r.basis     = static_cast<PayrollRule::Basis>    (q.value(3).toInt());
    r.value     = q.value(4).toDouble();
    r.enabled   = q.value(5).toBool();
    r.sortOrder = q.value(6).toInt();
    r.appliesTo = static_cast<PayrollRule::AppliesTo>(q.value(7).toInt());
    return r;
}

QVector<PayrollRule> PayrollRulesRepository::getAllRules() const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT id, name, type, basis, value, enabled, sort_order, applies_to "
              "FROM payroll_rules ORDER BY applies_to ASC, sort_order ASC, id ASC");
    QVector<PayrollRule> rules;
    if (!q.exec()) { m_lastError = q.lastError().text(); return rules; }
    while (q.next()) rules.append(fromQuery(q));
    return rules;
}

QVector<PayrollRule> PayrollRulesRepository::getRulesForTab(
        PayrollRule::AppliesTo tab) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT id, name, type, basis, value, enabled, sort_order, applies_to "
              "FROM payroll_rules WHERE applies_to=:tab "
              "ORDER BY sort_order ASC, id ASC");
    q.bindValue(":tab", static_cast<int>(tab));
    QVector<PayrollRule> rules;
    if (!q.exec()) { m_lastError = q.lastError().text(); return rules; }
    while (q.next()) rules.append(fromQuery(q));
    return rules;
}

QVector<PayrollRule> PayrollRulesRepository::getEnabledRulesForPayType(
        PayType type) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    // Returns All rules + rules matching the specific pay type
    const int payTypeVal = (type == PayType::Monthly)
                               ? static_cast<int>(PayrollRule::AppliesTo::Monthly)
                               : static_cast<int>(PayrollRule::AppliesTo::Hourly);
    q.prepare("SELECT id, name, type, basis, value, enabled, sort_order, applies_to "
              "FROM payroll_rules "
              "WHERE enabled=1 AND (applies_to=0 OR applies_to=:pt) "
              "ORDER BY sort_order ASC, id ASC");
    q.bindValue(":pt", payTypeVal);
    QVector<PayrollRule> rules;
    if (!q.exec()) { m_lastError = q.lastError().text(); return rules; }
    while (q.next()) rules.append(fromQuery(q));
    return rules;
}

QVector<PayrollRule> PayrollRulesRepository::getEnabledRules() const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT id, name, type, basis, value, enabled, sort_order, applies_to "
              "FROM payroll_rules WHERE enabled=1 "
              "ORDER BY sort_order ASC, id ASC");
    QVector<PayrollRule> rules;
    if (!q.exec()) { m_lastError = q.lastError().text(); return rules; }
    while (q.next()) rules.append(fromQuery(q));
    return rules;
}

bool PayrollRulesRepository::addRule(PayrollRule& rule) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("INSERT INTO payroll_rules "
              "(name, type, basis, value, enabled, sort_order, applies_to) "
              "VALUES (:name, :type, :basis, :value, :enabled, :sort, :applies)");
    q.bindValue(":name",    rule.name);
    q.bindValue(":type",    static_cast<int>(rule.type));
    q.bindValue(":basis",   static_cast<int>(rule.basis));
    q.bindValue(":value",   rule.value);
    q.bindValue(":enabled", rule.enabled ? 1 : 0);
    q.bindValue(":sort",    rule.sortOrder);
    q.bindValue(":applies", static_cast<int>(rule.appliesTo));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "addRule failed:" << m_lastError;
        return false;
    }
    rule.id = q.lastInsertId().toInt();
    return true;
}

bool PayrollRulesRepository::updateRule(const PayrollRule& rule) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE payroll_rules SET name=:name, type=:type, basis=:basis, "
              "value=:value, enabled=:enabled, sort_order=:sort, "
              "applies_to=:applies WHERE id=:id");
    q.bindValue(":name",    rule.name);
    q.bindValue(":type",    static_cast<int>(rule.type));
    q.bindValue(":basis",   static_cast<int>(rule.basis));
    q.bindValue(":value",   rule.value);
    q.bindValue(":enabled", rule.enabled ? 1 : 0);
    q.bindValue(":sort",    rule.sortOrder);
    q.bindValue(":applies", static_cast<int>(rule.appliesTo));
    q.bindValue(":id",      rule.id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "updateRule failed:" << m_lastError;
        return false;
    }
    return true;
}

bool PayrollRulesRepository::deleteRule(int id) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("DELETE FROM payroll_rules WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}
