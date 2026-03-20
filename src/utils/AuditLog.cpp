#include "utils/AuditLog.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDebug>

namespace AuditLog {

// ── Helpers ───────────────────────────────────────────────────────────────

static DatabaseManager& db() {
    return DatabaseManager::instance();
}

// Returns the chain_hash of the last entry, or "GENESIS" if the log is empty.
static QString getLastChainHash() {
    QSqlQuery q(db().database());
    q.prepare("SELECT chain_hash FROM audit_log ORDER BY id DESC LIMIT 1");
    if (q.exec() && q.next()) {
        const QString h = q.value(0).toString();
        if (!h.isEmpty()) return h;
    }
    return QStringLiteral("GENESIS");
}

// Compute the chain hash for a new entry given the previous hash and content.
static QString computeChainHash(const QString& prevHash,
                                 const QString& timestamp,
                                 const QString& action,
                                 const QString& entity,
                                 int            entityId,
                                 const QString& detail,
                                 const QString& oldValue,
                                 const QString& newValue)
{
    const QString content = prevHash
        + timestamp
        + action
        + entity
        + QString::number(entityId)
        + detail
        + oldValue
        + newValue;
    return QString::fromLatin1(
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// ── Public API ────────────────────────────────────────────────────────────

void record(const QString& action,
            const QString& entity,
            int            entityId,
            const QString& detail,
            const QString& oldValue,
            const QString& newValue)
{
    if (!db().isOpen()) {
        qWarning() << "AuditLog::record: DB not open, skipping:" << action;
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString prevHash  = getLastChainHash();
    const QString chainHash = computeChainHash(
        prevHash, timestamp, action, entity, entityId, detail, oldValue, newValue);

    QSqlQuery q(db().database());
    q.prepare(
        "INSERT INTO audit_log "
        "(timestamp, action, entity, entity_id, detail, old_value, new_value, chain_hash) "
        "VALUES (:ts, :action, :entity, :eid, :detail, :old, :new, :hash)");
    q.bindValue(":ts",     timestamp);
    q.bindValue(":action", action);
    q.bindValue(":entity", entity);
    q.bindValue(":eid",    entityId);
    q.bindValue(":detail", detail);
    q.bindValue(":old",    oldValue);
    q.bindValue(":new",    newValue);
    q.bindValue(":hash",   chainHash);

    if (!q.exec())
        qWarning() << "AuditLog::record failed:" << q.lastError().text()
                   << "action:" << action;
}

bool verify(int& brokenAtId) {
    brokenAtId = -1;

    QSqlQuery q(db().database());
    q.prepare(
        "SELECT id, timestamp, action, entity, entity_id, "
        "detail, old_value, new_value, chain_hash "
        "FROM audit_log ORDER BY id ASC");

    if (!q.exec()) {
        qWarning() << "AuditLog::verify query failed:" << q.lastError().text();
        return false;
    }

    QString prevHash = QStringLiteral("GENESIS");

    while (q.next()) {
        const int     id        = q.value(0).toInt();
        const QString timestamp = q.value(1).toString();
        const QString action    = q.value(2).toString();
        const QString entity    = q.value(3).toString();
        const int     entityId  = q.value(4).toInt();
        const QString detail    = q.value(5).toString();
        const QString oldValue  = q.value(6).toString();
        const QString newValue  = q.value(7).toString();
        const QString stored    = q.value(8).toString();

        const QString expected = computeChainHash(
            prevHash, timestamp, action, entity, entityId,
            detail, oldValue, newValue);

        if (stored != expected) {
            brokenAtId = id;
            qWarning() << "AuditLog::verify: chain broken at entry id" << id;
            return false;
        }

        prevHash = stored;
    }

    return true;
}

} // namespace AuditLog
