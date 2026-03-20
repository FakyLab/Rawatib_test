#include "repositories/EmployeeRepository.h"
#include "utils/EmployeePinManager.h"
#include "utils/AuditLog.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

EmployeeRepository& EmployeeRepository::instance() {
    static EmployeeRepository inst;
    return inst;
}

QString EmployeeRepository::lastError() const {
    return m_lastError;
}

// ── Helper: bind all monthly fields to a query ────────────────────────────
static void bindMonthlyFields(QSqlQuery& q, const Employee& e) {
    q.bindValue(":pay_type",   static_cast<int>(e.payType));
    q.bindValue(":monthly",    e.monthlySalary);
    q.bindValue(":work_days",  e.workingDaysPerMonth);
    q.bindValue(":exp_ci",     e.expectedCheckin.isValid()
                                   ? e.expectedCheckin.toString("HH:mm")
                                   : QVariant(QMetaType(QMetaType::QString)));
    q.bindValue(":exp_co",     e.expectedCheckout.isValid()
                                   ? e.expectedCheckout.toString("HH:mm")
                                   : QVariant(QMetaType(QMetaType::QString)));
    q.bindValue(":late_tol",   e.lateToleranceMin);
}

// ── Helper: populate monthly fields from query columns ───────────────────
// Assumes column order: ..., pay_type(7), monthly_salary(8),
//   working_days_per_month(9), expected_checkin(10), expected_checkout(11),
//   late_tolerance_min(12)
static void readMonthlyFields(QSqlQuery& q, Employee& e) {
    e.payType              = static_cast<PayType>(q.value(7).toInt());
    e.monthlySalary        = q.value(8).toDouble();
    e.workingDaysPerMonth  = q.value(9).toInt();
    e.expectedCheckin      = q.value(10).isNull()
                                 ? QTime()
                                 : QTime::fromString(q.value(10).toString(), "HH:mm");
    e.expectedCheckout     = q.value(11).isNull()
                                 ? QTime()
                                 : QTime::fromString(q.value(11).toString(), "HH:mm");
    e.lateToleranceMin     = q.value(12).toInt();
}

static const QString kSelectCols =
    "id, name, phone, hourly_wage, notes, pin_hash, pin_salt, "
    "pay_type, monthly_salary, working_days_per_month, "
    "expected_checkin, expected_checkout, late_tolerance_min";

bool EmployeeRepository::addEmployee(Employee& employee) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare(
        "INSERT INTO employees "
        "(name, phone, hourly_wage, notes, "
        " pay_type, monthly_salary, working_days_per_month, "
        " expected_checkin, expected_checkout, late_tolerance_min) "
        "VALUES (:name, :phone, :wage, :notes, "
        "        :pay_type, :monthly, :work_days, "
        "        :exp_ci, :exp_co, :late_tol)");
    q.bindValue(":name",  employee.name.trimmed());
    q.bindValue(":phone", employee.phone.trimmed());
    q.bindValue(":wage",  employee.hourlyWage);
    q.bindValue(":notes", employee.notes.trimmed());
    bindMonthlyFields(q, employee);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "addEmployee failed:" << m_lastError;
        return false;
    }
    employee.id = q.lastInsertId().toInt();
    return true;
}

bool EmployeeRepository::updateEmployee(const Employee& employee) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare(
        "UPDATE employees SET "
        "name=:name, phone=:phone, hourly_wage=:wage, notes=:notes, "
        "pay_type=:pay_type, monthly_salary=:monthly, "
        "working_days_per_month=:work_days, "
        "expected_checkin=:exp_ci, expected_checkout=:exp_co, "
        "late_tolerance_min=:late_tol "
        "WHERE id=:id");
    q.bindValue(":name",  employee.name.trimmed());
    q.bindValue(":phone", employee.phone.trimmed());
    q.bindValue(":wage",  employee.hourlyWage);
    q.bindValue(":notes", employee.notes.trimmed());
    q.bindValue(":id",    employee.id);
    bindMonthlyFields(q, employee);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "updateEmployee failed:" << m_lastError;
        return false;
    }
    return true;
}

bool EmployeeRepository::deleteEmployee(int id) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("DELETE FROM employees WHERE id=:id");
    q.bindValue(":id", id);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "deleteEmployee failed:" << m_lastError;
        return false;
    }
    return true;
}

std::optional<Employee> EmployeeRepository::getEmployee(int id) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT " + kSelectCols + " FROM employees WHERE id=:id");
    q.bindValue(":id", id);

    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        return std::nullopt;
    }

    Employee e;
    e.id         = q.value(0).toInt();
    e.name       = q.value(1).toString();
    e.phone      = q.value(2).toString();
    e.hourlyWage = q.value(3).toDouble();
    e.notes      = q.value(4).toString();
    e.pinHash    = q.value(5).toString();
    e.pinSalt    = q.value(6).toString();
    readMonthlyFields(q, e);
    return e;
}

QVector<Employee> EmployeeRepository::getAllEmployees() const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT " + kSelectCols + " FROM employees ORDER BY name ASC");

    QVector<Employee> employees;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return employees;
    }

    while (q.next()) {
        Employee e;
        e.id         = q.value(0).toInt();
        e.name       = q.value(1).toString();
        e.phone      = q.value(2).toString();
        e.hourlyWage = q.value(3).toDouble();
        e.notes      = q.value(4).toString();
        e.pinHash    = q.value(5).toString();
        e.pinSalt    = q.value(6).toString();
        readMonthlyFields(q, e);
        employees.append(e);
    }
    return employees;
}

QVector<Employee> EmployeeRepository::searchEmployees(const QString& query) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    const QString pattern = "%" + query.trimmed() + "%";
    q.prepare("SELECT " + kSelectCols +
              " FROM employees WHERE name LIKE :q OR phone LIKE :q ORDER BY name ASC");
    q.bindValue(":q", pattern);

    QVector<Employee> employees;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return employees;
    }

    while (q.next()) {
        Employee e;
        e.id         = q.value(0).toInt();
        e.name       = q.value(1).toString();
        e.phone      = q.value(2).toString();
        e.hourlyWage = q.value(3).toDouble();
        e.notes      = q.value(4).toString();
        e.pinHash    = q.value(5).toString();
        e.pinSalt    = q.value(6).toString();
        readMonthlyFields(q, e);
        employees.append(e);
    }
    return employees;
}

bool EmployeeRepository::setEmployeePin(int employeeId, const QString& pin) {
    if (pin.isEmpty())
        return clearEmployeePin(employeeId);

    if (!EmployeePinManager::isValidPin(pin)) {
        m_lastError = "PIN must be 6-12 digits.";
        return false;
    }

    const QByteArray salt    = EmployeePinManager::generateSalt();
    const QString    saltHex = QString::fromLatin1(salt.toHex());
    const QString    hash    = EmployeePinManager::hashPin(pin, salt);

    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE employees SET pin_hash=:hash, pin_salt=:salt WHERE id=:id");
    q.bindValue(":hash", hash);
    q.bindValue(":salt", saltHex);
    q.bindValue(":id",   employeeId);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "setEmployeePin failed:" << m_lastError;
        return false;
    }
    return true;
}

bool EmployeeRepository::clearEmployeePin(int employeeId) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("UPDATE employees SET pin_hash='', pin_salt='' WHERE id=:id");
    q.bindValue(":id", employeeId);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "clearEmployeePin failed:" << m_lastError;
        return false;
    }
    return true;
}
