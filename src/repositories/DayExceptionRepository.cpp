#include "repositories/DayExceptionRepository.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

DayExceptionRepository& DayExceptionRepository::instance() {
    static DayExceptionRepository inst;
    return inst;
}

QString DayExceptionRepository::lastError() const {
    return m_lastError;
}

bool DayExceptionRepository::addException(DayException& ex) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    // INSERT OR IGNORE so duplicate date+employee combinations are silently
    // ignored — the unique constraint prevents double-counting.
    q.prepare(
        "INSERT OR IGNORE INTO day_exceptions (date, employee_id, reason) "
        "VALUES (:date, :emp, :reason)");
    q.bindValue(":date",   ex.date.toString(Qt::ISODate));
    q.bindValue(":emp",    ex.employeeId > 0
                               ? QVariant(ex.employeeId)
                               : QVariant(QMetaType(QMetaType::Int)));   // NULL
    q.bindValue(":reason", ex.reason.trimmed());

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "addException failed:" << m_lastError;
        return false;
    }
    ex.id = q.lastInsertId().toInt();
    return true;
}

bool DayExceptionRepository::removeException(int id) {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("DELETE FROM day_exceptions WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "removeException failed:" << m_lastError;
        return false;
    }
    return true;
}

QVector<DayException> DayExceptionRepository::getAll() const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare(
        "SELECT id, date, employee_id, reason "
        "FROM day_exceptions ORDER BY date ASC, employee_id ASC");

    QVector<DayException> result;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return result;
    }
    while (q.next()) {
        DayException ex;
        ex.id         = q.value(0).toInt();
        ex.date       = QDate::fromString(q.value(1).toString(), Qt::ISODate);
        ex.employeeId = q.value(2).isNull() ? 0 : q.value(2).toInt();
        ex.reason     = q.value(3).toString();
        result.append(ex);
    }
    return result;
}

int DayExceptionRepository::countExceptionDays(int employeeId,
                                                const QDate& startDate,
                                                const QDate& endDate) const {
    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    // Count distinct dates that have a matching exception.
    // Matches: company-wide (employee_id IS NULL) OR specific to this employee.
    q.prepare(R"(
        SELECT COUNT(DISTINCT date)
        FROM day_exceptions
        WHERE date BETWEEN :start AND :end
          AND (employee_id IS NULL OR employee_id = :emp)
    )");
    q.bindValue(":start", startDate.toString(Qt::ISODate));
    q.bindValue(":end",   endDate.toString(Qt::ISODate));
    q.bindValue(":emp",   employeeId);

    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}
