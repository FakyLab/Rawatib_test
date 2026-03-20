#pragma once
#include <QSqlDatabase>
#include <QString>

class DatabaseManager {
public:
    static DatabaseManager& instance();

    // Opens (or creates) the database.
    // keyOverride: used by --bypass-key only — passes the old user key directly
    // so initialize() can open the still-encrypted DB before rekeying to fallback.
    bool initialize(const QString& dbPath = "attendance.db",
                    const QByteArray& keyOverride = {});

    // Tries to open an already-located DB using a key derived from the given PIN.
    // Called when initialize() fails due to a missing/wrong keychain entry —
    // e.g. after an OS reinstall, machine migration, or restoring a backup
    // that was made with a different PIN.
    //
    // On success: stores the derived key in the OS keychain and returns true.
    // On failure: closes the connection and returns false (wrong PIN).
    // Safe to call repeatedly — the DB file is never modified.
    bool tryUnlockWithPin(const QString& pin);

    QSqlDatabase& database();
    bool          isOpen()      const;
    QString       lastError()   const;
    QString       databasePath() const { return m_dbPath; }

    // Re-applies the effective key after any external close/open cycle.
    // Now also verifies the key worked — returns false if the key is wrong.
    bool reapplyKey();

    // PIN set: rekey from fallback → user key
    bool encryptDatabase(const QByteArray& newKey);

    // PIN changed: rekey from old user key → new user key
    bool rekeyDatabase(const QByteArray& oldKey, const QByteArray& newKey);

    // PIN removed: rekey from user key → fallback key
    bool decryptDatabase(const QByteArray& currentKey);

    // The compiled-in fallback key used when no PIN is set.
    static QByteArray fallbackKey();

private:
    DatabaseManager() = default;
    ~DatabaseManager() = default;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool applyKey(const QByteArray& key);
    void reopenConnection();
    bool tryMigrateFromPlain();
    bool createTables();
    bool enableForeignKeys();
    bool createPayrollRulesTable();
    bool createDayExceptionsTable();
    bool createAppSettingsTable();

public:
    QString getDbSetting(const QString& key, const QString& defaultVal = {}) const;
    bool    setDbSetting(const QString& key, const QString& value);
    bool    removeDbSetting(const QString& key);
    bool    hasDbSetting(const QString& key) const;

    QSqlDatabase m_db;
    QString      m_lastError;
    QString      m_dbPath;
};
