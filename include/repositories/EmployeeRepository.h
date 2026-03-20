#pragma once
#include "models/Employee.h"
#include <QVector>
#include <QString>
#include <optional>

class EmployeeRepository {
public:
    static EmployeeRepository& instance();

    bool addEmployee(Employee& employee);
    bool updateEmployee(const Employee& employee);
    bool deleteEmployee(int id);
    std::optional<Employee> getEmployee(int id) const;
    QVector<Employee> getAllEmployees() const;
    QVector<Employee> searchEmployees(const QString& query) const;

    // Employee PIN management — stores hashed PIN in employees table.
    // Pass empty pin to clear (same as clearEmployeePin).
    bool setEmployeePin(int employeeId, const QString& pin);
    bool clearEmployeePin(int employeeId);

    QString lastError() const;

private:
    EmployeeRepository() = default;
    mutable QString m_lastError;
};
