#include "database/DatabaseManager.h"
#include "database/Migrations.h"
#include "utils/PinManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include <QDir>
#include <QFile>

// ── Fallback key ──────────────────────────────────────────────────────────
// The database is ALWAYS encrypted — there is no plain-SQLite state.
// When no PIN is set, this compiled-in key is used.
// Derived as SHA-256 of a fixed app-specific string so it is always
// exactly 32 bytes in the correct format.
// Being open-source, this key is visible in the source, which is acceptable:
// it protects against casual file copying, not against a determined attacker
// who has already read the source code and has full machine access.

QByteArray DatabaseManager::fallbackKey() {
    return QCryptographicHash::hash(
        QByteArray("Rawatib-AttendanceDB-DefaultKey-v1-molnupiravir-faky"),
        QCryptographicHash::Sha256
    );
}

// ── Singleton ─────────────────────────────────────────────────────────────

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

// ── Connection helper ─────────────────────────────────────────────────────
// Always remove the old named connection before creating a new one.
// Qt's addDatabase() returns the existing connection if the name is already
// registered — without removeDatabase() first, the handle is stale after
// any close() call.

void DatabaseManager::reopenConnection() {
    if (QSqlDatabase::contains("main_connection")) {
        m_db = QSqlDatabase();                       // release our reference
        QSqlDatabase::removeDatabase("main_connection");
    }
    m_db = QSqlDatabase::addDatabase("QSQLCIPHER", "main_connection");
}

// ── Key helper ────────────────────────────────────────────────────────────
// Executes PRAGMA key = "x'<hex>'" immediately after open().
// Must be the very first query on the connection.
// Empty key is only passed during tryMigrateFromPlain() to probe a legacy
// plain-SQLite file — in all normal flows a key is always provided.

bool DatabaseManager::applyKey(const QByteArray& key) {
    if (key.isEmpty()) return true;   // probe mode — skip PRAGMA key
    QSqlQuery q(m_db);
    const QString hexKey = QString::fromLatin1(key.toHex());
    if (!q.exec(QString("PRAGMA key = \"x'%1'\";").arg(hexKey))) {
        m_lastError = q.lastError().text();
        qCritical() << "PRAGMA key failed:" << m_lastError;
        return false;
    }
    return true;
}

// ── reapplyKey ────────────────────────────────────────────────────────────
// Called after any external close()/open() cycle on the existing connection
// (AutoBackupManager startup backup, MainWindow backup/restore handlers).
// Determines the correct key — user key from keychain, or fallback —
// applies it, then verifies with a test query.
// Returns false if the key is wrong (e.g. restored DB has a different key).

bool DatabaseManager::reapplyKey() {
    const QByteArray key = PinManager::loadKey();
    if (!applyKey(key.isEmpty() ? fallbackKey() : key))
        return false;
    // Verify the key actually opened the DB correctly
    QSqlQuery test(m_db);
    if (!test.exec("SELECT count(*) FROM sqlite_master;")) {
        m_lastError = "reapplyKey: key verification failed — DB may use a different key.";
        qWarning() << m_lastError;
        return false;
    }
    return true;
}

// ── tryUnlockWithPin ──────────────────────────────────────────────────────
// Called when initialize() failed due to a wrong/missing keychain key.
// Derives the SQLCipher key from the given PIN, tries to open the DB,
// and on success stores the derived key in the OS keychain so subsequent
// launches work silently.
//
// Does NOT call createTables() — that is handled by initialize() after
// this returns true. Instead we call enableForeignKeys() and createTables()
// directly here so the DB is fully ready.

bool DatabaseManager::tryUnlockWithPin(const QString& pin) {
    const QByteArray key = PinManager::deriveKey(pin);

    reopenConnection();
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        m_lastError = "tryUnlockWithPin: failed to open DB — " + m_db.lastError().text();
        qWarning() << m_lastError;
        return false;
    }

    if (!applyKey(key)) {
        m_db.close();
        return false;
    }

    // Verify the key is correct
    {
        QSqlQuery test(m_db);
        if (!test.exec("SELECT count(*) FROM sqlite_master;")) {
            m_lastError = "tryUnlockWithPin: wrong PIN — key verification failed.";
            qWarning() << m_lastError;
            m_db.close();
            return false;
        }
    }

    // Key is correct — store it in keychain so next launch is silent
    if (!PinManager::storeKey(key))
        qWarning() << "tryUnlockWithPin: key derived OK but keychain store failed.";

    // Complete DB setup (idempotent — safe to call on existing DB)
    if (!enableForeignKeys()) return false;
    if (!createTables())      return false;

    qDebug() << "tryUnlockWithPin: DB unlocked successfully. Keychain updated.";
    return true;
}

// ── initialize ────────────────────────────────────────────────────────────

bool DatabaseManager::initialize(const QString& dbPath,
                                  const QByteArray& keyOverride) {
    m_dbPath = dbPath;

    reopenConnection();
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        qCritical() << "Failed to open database:" << m_lastError;
        return false;
    }

    // Determine which key to use:
    //   1. keyOverride — bypass-pin flow passes the old user key directly
    //      because the keychain entry was already deleted before this call.
    //   2. keychain — normal launch with PIN set.
    //   3. fallbackKey — normal launch with no PIN set.
    QByteArray key;
    if (!keyOverride.isEmpty()) {
        key = keyOverride;
    } else {
        key = PinManager::loadKey();
        if (key.isEmpty())
            key = fallbackKey();
    }

    if (!applyKey(key)) return false;

    // Verify: if this fails, either the key is wrong or the file is a legacy
    // plain-SQLite database from an older build.
    {
        QSqlQuery test(m_db);
        if (!test.exec("SELECT count(*) FROM sqlite_master;")) {
            qDebug() << "Key verification failed — attempting legacy migration.";
            if (!tryMigrateFromPlain()) {
                m_lastError = "Database key is invalid or the file is corrupt.";
                qCritical() << m_lastError;
                m_db.close();
                return false;
            }
            // Migration succeeded — DB is now encrypted with fallbackKey().
        }
    }

    if (!enableForeignKeys()) return false;
    if (!createTables())      return false;

    qDebug() << "Database initialized successfully:" << dbPath
             << "(always-encrypted)";
    return true;
}

// ── tryMigrateFromPlain ───────────────────────────────────────────────────
// One-time upgrade path for users who installed an older build that left
// the database as plain SQLite.
//
// Strategy:
//   1. Close current connection (which just failed the key test).
//   2. Reopen with NO PRAGMA key — if the test query now succeeds, the file
//      is plain SQLite.
//   3. From that plain connection, ATTACH a new encrypted output file using
//      fallbackKey().  Export in the correct direction: plain main ->
//      encrypted attached.  This is the only sqlcipher_export call in the
//      entire codebase, and it is in the direction SQLCipher allows.
//   4. Replace the original file with the encrypted copy.
//   5. Reopen with fallbackKey() and verify.
//
// After this runs once the file is always in SQLCipher format and this
// function is never triggered again.

bool DatabaseManager::tryMigrateFromPlain() {
    m_db.close();

    // Step 1: probe as plain SQLite (no PRAGMA key)
    reopenConnection();
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        m_lastError = "Migration probe: failed to reopen — " +
                      m_db.lastError().text();
        return false;
    }

    {
        QSqlQuery probe(m_db);
        if (!probe.exec("SELECT count(*) FROM sqlite_master;")) {
            // Not plain SQLite either — genuinely corrupt or wrong key.
            m_db.close();
            m_lastError = "Not a plain SQLite file and key is incorrect.";
            return false;
        }
    }

    qDebug() << "Legacy plain SQLite detected — migrating to always-encrypted.";

    // Step 2: export plain -> encrypted (correct sqlcipher_export direction)
    const QString tmpPath = m_dbPath + ".migrating";
    const QString tmpSql  = QDir::fromNativeSeparators(tmpPath);
    const QString hexKey  = QString::fromLatin1(fallbackKey().toHex());

    QSqlQuery q(m_db);

    if (!q.exec(QString("ATTACH DATABASE '%1' AS encrypted KEY \"x'%2'\";")
                .arg(tmpSql, hexKey))) {
        m_lastError = "Migration ATTACH failed: " + q.lastError().text();
        qCritical() << m_lastError;
        m_db.close();
        return false;
    }

    if (!q.exec("SELECT sqlcipher_export('encrypted');")) {
        m_lastError = "Migration sqlcipher_export failed: " + q.lastError().text();
        qCritical() << m_lastError;
        q.exec("DETACH DATABASE encrypted;");
        QFile::remove(tmpPath);
        m_db.close();
        return false;
    }

    q.exec("DETACH DATABASE encrypted;");
    m_db.close();

    // Step 3: replace original with encrypted copy
    QFile::remove(m_dbPath);
    if (!QFile::rename(tmpPath, m_dbPath)) {
        m_lastError = "Migration rename failed.";
        qCritical() << m_lastError;
        return false;
    }

    // Step 4: reopen with fallbackKey() and verify
    reopenConnection();
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        m_lastError = "Migration reopen failed: " + m_db.lastError().text();
        qCritical() << m_lastError;
        return false;
    }
    if (!applyKey(fallbackKey())) return false;

    {
        QSqlQuery verify(m_db);
        if (!verify.exec("SELECT count(*) FROM sqlite_master;")) {
            m_lastError = "Migration verify failed: " + verify.lastError().text();
            qCritical() << m_lastError;
            m_db.close();
            return false;
        }
    }

    qDebug() << "Legacy migration completed — database is now always-encrypted.";
    return true;
}

// ── encryptDatabase ───────────────────────────────────────────────────────
// Called when the admin sets a PIN for the first time.
// DB is open and currently encrypted with fallbackKey().
// PRAGMA rekey changes it to the user's PIN-derived key.

bool DatabaseManager::encryptDatabase(const QByteArray& newKey) {
    QSqlQuery q(m_db);
    const QString hexKey = QString::fromLatin1(newKey.toHex());
    if (!q.exec(QString("PRAGMA rekey = \"x'%1'\";").arg(hexKey))) {
        m_lastError = "PRAGMA rekey (set PIN) failed: " + q.lastError().text();
        qCritical() << m_lastError;
        return false;
    }
    qDebug() << "Database rekeyed to user key (PIN set).";
    return true;
}

// ── rekeyDatabase ─────────────────────────────────────────────────────────
// Called when the admin changes their PIN.
// DB is open and encrypted with the old user key.
// PRAGMA rekey changes it to the new user key.

bool DatabaseManager::rekeyDatabase(const QByteArray& /*oldKey*/,
                                     const QByteArray& newKey) {
    QSqlQuery q(m_db);
    const QString hexKey = QString::fromLatin1(newKey.toHex());
    if (!q.exec(QString("PRAGMA rekey = \"x'%1'\";").arg(hexKey))) {
        m_lastError = "PRAGMA rekey (change PIN) failed: " + q.lastError().text();
        qCritical() << m_lastError;
        return false;
    }
    qDebug() << "Database rekeyed to new user key (PIN changed).";
    return true;
}

// ── decryptDatabase ───────────────────────────────────────────────────────
// Called when the admin removes their PIN.
// DB is open and encrypted with the user key.
// PRAGMA rekey changes it back to fallbackKey().
// The DB remains encrypted — it just returns to the default no-PIN state.
// This operation cannot corrupt the database.

bool DatabaseManager::decryptDatabase(const QByteArray& /*currentKey*/) {
    QSqlQuery q(m_db);
    const QString hexKey = QString::fromLatin1(fallbackKey().toHex());
    if (!q.exec(QString("PRAGMA rekey = \"x'%1'\";").arg(hexKey))) {
        m_lastError = "PRAGMA rekey (remove PIN) failed: " + q.lastError().text();
        qCritical() << m_lastError;
        return false;
    }
    qDebug() << "Database rekeyed to fallback key (PIN removed).";
    return true;
}

// ── Standard helpers ──────────────────────────────────────────────────────

QSqlDatabase& DatabaseManager::database() { return m_db; }
bool DatabaseManager::isOpen()      const  { return m_db.isOpen(); }
QString DatabaseManager::lastError() const { return m_lastError; }

bool DatabaseManager::enableForeignKeys() {
    QSqlQuery q(m_db);
    if (!q.exec("PRAGMA foreign_keys = ON;")) {
        m_lastError = q.lastError().text();
        qCritical() << "PRAGMA foreign_keys failed:" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::createTables() {
    QSqlQuery q(m_db);

    const QString createEmployees = R"(
        CREATE TABLE IF NOT EXISTS employees (
            id                      INTEGER PRIMARY KEY AUTOINCREMENT,
            name                    TEXT    NOT NULL,
            phone                   TEXT    DEFAULT '',
            hourly_wage             REAL    NOT NULL DEFAULT 0.0,
            notes                   TEXT    DEFAULT '',
            pin_hash                TEXT    NOT NULL DEFAULT '',
            pin_salt                TEXT    NOT NULL DEFAULT '',
            pay_type                INTEGER NOT NULL DEFAULT 0,
            monthly_salary          REAL    NOT NULL DEFAULT 0.0,
            working_days_per_month  INTEGER NOT NULL DEFAULT 26,
            expected_checkin        TEXT    DEFAULT NULL,
            expected_checkout       TEXT    DEFAULT NULL,
            late_tolerance_min      INTEGER NOT NULL DEFAULT 15
        );
    )";

    const QString createAttendance = R"(
        CREATE TABLE IF NOT EXISTS attendance (
            id                    INTEGER PRIMARY KEY AUTOINCREMENT,
            employee_id           INTEGER NOT NULL,
            date                  TEXT    NOT NULL,
            check_in              TEXT    NOT NULL,
            check_out             TEXT    DEFAULT NULL,
            hours                 REAL    DEFAULT NULL,
            base_daily_rate       REAL    NOT NULL DEFAULT 0.0,
            day_deduction         REAL    NOT NULL DEFAULT 0.0,
            daily_wage            REAL    DEFAULT NULL,
            paid_status           INTEGER NOT NULL DEFAULT 0,
            late_minutes          INTEGER NOT NULL DEFAULT 0,
            early_minutes         INTEGER NOT NULL DEFAULT 0,
            applied_deduction_mode TEXT    DEFAULT NULL,
            FOREIGN KEY (employee_id) REFERENCES employees(id) ON DELETE CASCADE
        );
    )";

    if (!q.exec(createEmployees)) {
        m_lastError = q.lastError().text();
        qCritical() << "Failed to create employees table:" << m_lastError;
        return false;
    }

    if (!q.exec(createAttendance)) {
        m_lastError = q.lastError().text();
        qCritical() << "Failed to create attendance table:" << m_lastError;
        return false;
    }

    q.exec("CREATE INDEX IF NOT EXISTS idx_attendance_employee "
           "ON attendance(employee_id);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_attendance_date "
           "ON attendance(date);");

    // ── Audit log ─────────────────────────────────────────────────────────
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS audit_log (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT    NOT NULL,
            action      TEXT    NOT NULL,
            entity      TEXT    DEFAULT '',
            entity_id   INTEGER DEFAULT 0,
            detail      TEXT    DEFAULT '',
            old_value   TEXT    DEFAULT '',
            new_value   TEXT    DEFAULT '',
            chain_hash  TEXT    NOT NULL DEFAULT ''
        );
    )")) {
        m_lastError = q.lastError().text();
        qCritical() << "Failed to create audit_log table:" << m_lastError;
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_audit_timestamp "
           "ON audit_log(timestamp);");

    // Fresh install — set schema version directly, no migrations needed
    q.exec(QString("PRAGMA user_version = %1")
               .arg(Migrations::CURRENT_VERSION));

    // Create remaining tables (idempotent, safe on every launch)
    if (!createPayrollRulesTable())   return false;
    if (!createDayExceptionsTable())  return false;
    if (!createAppSettingsTable())    return false;

    // Run any pending post-ship migrations (none yet for v1.0)
    Migrations::run(m_db);

    return true;
}

// ── createPayrollRulesTable ───────────────────────────────────────────────
// Creates the payroll_rules table on fresh install. IF NOT EXISTS makes it
// safe to call even if the table already exists.

bool DatabaseManager::createPayrollRulesTable() {
    QSqlQuery q(m_db);
    const QString sql = R"(
        CREATE TABLE IF NOT EXISTS payroll_rules (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            name       TEXT    NOT NULL,
            type       INTEGER NOT NULL DEFAULT 0,
            basis      INTEGER NOT NULL DEFAULT 0,
            value      REAL    NOT NULL DEFAULT 0.0,
            enabled    INTEGER NOT NULL DEFAULT 1,
            sort_order INTEGER NOT NULL DEFAULT 0,
            applies_to INTEGER NOT NULL DEFAULT 0
        );
    )";
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        qCritical() << "Failed to create payroll_rules table:" << m_lastError;
        return false;
    }
    return true;
}

// ── createDayExceptionsTable ──────────────────────────────────────────────
// Stores admin-defined day exceptions — dates that should not count as
// absent for monthly salary employees (public holidays, approved leave, etc.)
// employee_id NULL = company-wide; specific value = individual employee.

bool DatabaseManager::createDayExceptionsTable() {
    QSqlQuery q(m_db);
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS day_exceptions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            date        TEXT    NOT NULL,
            employee_id INTEGER DEFAULT NULL,
            reason      TEXT    NOT NULL DEFAULT ''
        );
    )")) {
        m_lastError = q.lastError().text();
        qCritical() << "Failed to create day_exceptions table:" << m_lastError;
        return false;
    }
    // Standard UNIQUE(date, employee_id) does not protect against duplicate
    // company-wide rows because SQLite treats NULL != NULL in UNIQUE constraints.
    // COALESCE maps NULL → -1 (never a real employee id) so two company-wide
    // rows on the same date are correctly seen as duplicates and blocked.
    q.exec(R"(
        CREATE UNIQUE INDEX IF NOT EXISTS idx_day_exceptions_unique
        ON day_exceptions(date, COALESCE(employee_id, -1));
    )");
    return true;
}

// ── createAppSettingsTable ────────────────────────────────────────────────
// Creates the app_settings table and generates install_secret on first run.
// Stores security-critical settings inside the encrypted database.

bool DatabaseManager::createAppSettingsTable() {
    QSqlQuery q(m_db);
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS app_settings (
            key   TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL DEFAULT ''
        );
    )")) {
        m_lastError = q.lastError().text();
        qCritical() << "Failed to create app_settings table:" << m_lastError;
        return false;
    }

    // ── install_secret ────────────────────────────────────────────────────
    // Generated once on first DB creation, never changed except after a
    // successful --bypass-key (rotateInstallSecret() in PinManager).
    // Survives PIN changes, app reinstalls, and OS reinstalls as long as
    // the DB file is preserved. Used to derive the recovery file token.
    if (!hasDbSetting("install_secret")) {
        QByteArray secret(32, Qt::Uninitialized);
        auto rng = QRandomGenerator::securelySeeded();
        rng.generate(reinterpret_cast<quint32*>(secret.data()),
                     reinterpret_cast<quint32*>(secret.data()) + secret.size() / sizeof(quint32));
        const QString secretHex = QString::fromLatin1(secret.toHex());
        if (!setDbSetting("install_secret", secretHex))
            qCritical() << "Failed to store install_secret in DB.";
        else
            qDebug() << "install_secret generated and stored.";
    }
    return true;
}

// ── DB setting helpers ────────────────────────────────────────────────────

QString DatabaseManager::getDbSetting(const QString& key, const QString& defaultVal) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM app_settings WHERE key=:key");
    q.bindValue(":key", key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultVal;
}

bool DatabaseManager::setDbSetting(const QString& key, const QString& value) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES (:key, :value)");
    q.bindValue(":key",   key);
    q.bindValue(":value", value);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "setDbSetting failed:" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::removeDbSetting(const QString& key) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM app_settings WHERE key=:key");
    q.bindValue(":key", key);
    return q.exec();
}

bool DatabaseManager::hasDbSetting(const QString& key) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM app_settings WHERE key=:key");
    q.bindValue(":key", key);
    if (q.exec() && q.next())
        return q.value(0).toInt() > 0;
    return false;
}
