#include "utils/PinManager.h"
#include "utils/AuditLog.h"
#include "database/DatabaseManager.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QDataStream>
#include <keychain.h>

namespace PinManager {

// ── Key names ─────────────────────────────────────────────────────────────
static constexpr const char* DB_PIN_HASH_KEY      = "admin_pin_hash";
static constexpr const char* DB_PIN_SALT_KEY      = "admin_pin_salt";
static constexpr const char* DB_CRED_VER_KEY      = "credential_version";
static constexpr const char* DB_RECOVERY_HASH_KEY = "recovery_token_hash";
static constexpr const char* DB_BYPASS_TS_KEY     = "pin_bypass_timestamp";
static constexpr const char* DB_BYPASS_SET_KEY    = "pin_bypass_pin_was_set";

// Rate limiter keys
static constexpr const char* DB_FAILED_ATTEMPTS   = "auth/failed_attempts";
static constexpr const char* DB_LOCKOUT_UNTIL     = "auth/lockout_until";

// Keychain constants
static constexpr const char* KEYCHAIN_SERVICE = "Rawatib";
static constexpr const char* KEYCHAIN_KEY     = "db_cipher_key";

// Credential version:
//   1 = PBKDF2-SHA256, 100k iterations, 16-byte random salt (current)
static constexpr int CURRENT_CREDENTIAL_VERSION = 1;

// Fixed salt for SQLCipher key derivation only — PIN hashing uses a
// separate random per-install salt stored in the DB.
static constexpr const char* CIPHER_KEY_SALT =
    "Rawatib-SQLCipher-Salt-v1-molnupiravir-faky";

// HMAC context string for recovery token derivation
static constexpr const char* RECOVERY_HMAC_CONTEXT = "rawatib-recovery-v1";

// ── DB accessor ───────────────────────────────────────────────────────────

static DatabaseManager& db() {
    return DatabaseManager::instance();
}

// ── PBKDF2-SHA256 ─────────────────────────────────────────────────────────

static QByteArray pbkdf2_sha256(const QByteArray& password,
                                 const QByteArray& salt,
                                 int iterations,
                                 int keyLen)
{
    QByteArray result;
    int blockNum = 0;
    while (result.size() < keyLen) {
        blockNum++;
        QByteArray input = salt;
        input.append(char((blockNum >> 24) & 0xFF));
        input.append(char((blockNum >> 16) & 0xFF));
        input.append(char((blockNum >>  8) & 0xFF));
        input.append(char( blockNum        & 0xFF));
        QByteArray U = QMessageAuthenticationCode::hash(
            input, password, QCryptographicHash::Sha256);
        QByteArray T = U;
        for (int i = 1; i < iterations; ++i) {
            U = QMessageAuthenticationCode::hash(
                U, password, QCryptographicHash::Sha256);
            for (int j = 0; j < T.size(); ++j)
                T[j] = T[j] ^ U[j];
        }
        result.append(T);
    }
    return result.left(keyLen);
}

// ── PIN hash ──────────────────────────────────────────────────────────────

static QString hashPin(const QString& pin, const QByteArray& salt) {
    return QString::fromLatin1(
        pbkdf2_sha256(pin.toUtf8(), salt, 100000, 32).toHex()
    );
}

// ── Random bytes ──────────────────────────────────────────────────────────

static QByteArray randomBytes(int count) {
    QByteArray buf(count, Qt::Uninitialized);
    auto rng = QRandomGenerator::securelySeeded();
    rng.generate(reinterpret_cast<quint32*>(buf.data()),
                 reinterpret_cast<quint32*>(buf.data()) + buf.size() / sizeof(quint32));
    return buf;
}

// ═══════════════════════════════════════════════════════════════════════════
// RecoveryFileHelper — binary file format for --bypass-key recovery
// ═══════════════════════════════════════════════════════════════════════════
//
// File layout (81 bytes total):
//   [0–7]   Magic:     "RWTREC01"  (8 bytes, ASCII)
//   [8]     Version:   0x01        (1 byte)
//   [9–16]  Timestamp: Unix epoch  (8 bytes, big-endian int64)
//   [17–48] Token:     HMAC-SHA256(install_secret, context) (32 bytes)
//   [49–80] Checksum:  SHA-256 of bytes [0–48]              (32 bytes)
//
// The token is derived deterministically from install_secret — same DB,
// same token, regardless of PIN changes or reinstalls.
// The DB stores SHA-256(token) — the file itself never lives in the DB.

namespace RecoveryFileHelper {

static constexpr int FILE_SIZE      = 81;
static constexpr int OFFSET_MAGIC   = 0;
static constexpr int OFFSET_VERSION = 8;
static constexpr int OFFSET_TS      = 9;
static constexpr int OFFSET_TOKEN   = 17;
static constexpr int OFFSET_CHKSUM  = 49;
static const QByteArray MAGIC       = QByteArray("RWTREC01");
static constexpr quint8 FORMAT_VER  = 0x01;

// Build the 81-byte recovery file from a token
QByteArray build(const QByteArray& token) {
    Q_ASSERT(token.size() == 32);

    QByteArray file(FILE_SIZE, 0x00);

    // Magic
    file.replace(OFFSET_MAGIC, 8, MAGIC);

    // Version
    file[OFFSET_VERSION] = static_cast<char>(FORMAT_VER);

    // Timestamp (big-endian int64)
    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    for (int i = 0; i < 8; ++i)
        file[OFFSET_TS + i] = static_cast<char>((ts >> (56 - i * 8)) & 0xFF);

    // Token
    file.replace(OFFSET_TOKEN, 32, token);

    // Checksum: SHA-256 of bytes [0–48]
    const QByteArray checksum = QCryptographicHash::hash(
        file.left(OFFSET_CHKSUM), QCryptographicHash::Sha256);
    file.replace(OFFSET_CHKSUM, 32, checksum);

    return file;
}

// Parse and validate the file. Returns the 32-byte token on success,
// empty QByteArray on any failure (wrong magic, bad version, bad checksum).
QByteArray parse(const QByteArray& data) {
    if (data.size() != FILE_SIZE) {
        qWarning() << "RecoveryFile: wrong size" << data.size();
        return {};
    }
    if (data.mid(OFFSET_MAGIC, 8) != MAGIC) {
        qWarning() << "RecoveryFile: bad magic";
        return {};
    }
    if (static_cast<quint8>(data[OFFSET_VERSION]) != FORMAT_VER) {
        qWarning() << "RecoveryFile: unsupported version"
                   << static_cast<int>(data[OFFSET_VERSION]);
        return {};
    }
    // Verify checksum
    const QByteArray expected = QCryptographicHash::hash(
        data.left(OFFSET_CHKSUM), QCryptographicHash::Sha256);
    if (data.mid(OFFSET_CHKSUM, 32) != expected) {
        qWarning() << "RecoveryFile: checksum mismatch — file is corrupt.";
        return {};
    }
    return data.mid(OFFSET_TOKEN, 32);
}

// Suggested save filename
QString suggestedFileName() {
    return "Rawatib_Recovery.rwtrec";
}

} // namespace RecoveryFileHelper

// ═══════════════════════════════════════════════════════════════════════════
// SQLCipher key management
// ═══════════════════════════════════════════════════════════════════════════

QByteArray deriveKey(const QString& pin) {
    return pbkdf2_sha256(
        pin.toUtf8(),
        QByteArray(CIPHER_KEY_SALT),
        100000,
        32
    );
}

bool storeKey(const QByteArray& key) {
    QEventLoop loop;
    bool success = false;
    auto* job = new QKeychain::WritePasswordJob(KEYCHAIN_SERVICE);
    job->setKey(KEYCHAIN_KEY);
    job->setBinaryData(key);
    job->setAutoDelete(true);
    QObject::connect(job, &QKeychain::Job::finished, [&](QKeychain::Job* j) {
        success = (j->error() == QKeychain::NoError);
        if (!success) qWarning() << "Keychain write failed:" << j->errorString();
        loop.quit();
    });
    job->start();
    loop.exec();
    return success;
}

QByteArray loadKey() {
    QEventLoop loop;
    QByteArray result;
    auto* job = new QKeychain::ReadPasswordJob(KEYCHAIN_SERVICE);
    job->setKey(KEYCHAIN_KEY);
    job->setAutoDelete(true);
    QObject::connect(job, &QKeychain::Job::finished, [&](QKeychain::Job* j) {
        if (j->error() == QKeychain::NoError)
            result = static_cast<QKeychain::ReadPasswordJob*>(j)->binaryData();
        else if (j->error() != QKeychain::EntryNotFound)
            qWarning() << "Keychain read failed:" << j->errorString();
        loop.quit();
    });
    job->start();
    loop.exec();
    return result;
}

void deleteKey() {
    QEventLoop loop;
    auto* job = new QKeychain::DeletePasswordJob(KEYCHAIN_SERVICE);
    job->setKey(KEYCHAIN_KEY);
    job->setAutoDelete(true);
    QObject::connect(job, &QKeychain::Job::finished, [&](QKeychain::Job*) {
        loop.quit();
    });
    job->start();
    loop.exec();
}

// ═══════════════════════════════════════════════════════════════════════════
// PIN — stored in encrypted DB
// ═══════════════════════════════════════════════════════════════════════════

bool isPinSet() {
    if (!db().isOpen())
        return !loadKey().isEmpty();
    return db().hasDbSetting(DB_PIN_HASH_KEY);
}

bool verifyPin(const QString& pin) {
    if (!isPinSet()) return false;

    // ── Rate limit check — before any crypto work ─────────────────────────
    if (getLockoutSeconds() > 0) return false;

    const int ver = db().getDbSetting(DB_CRED_VER_KEY, "1").toInt();

    bool verified = false;
    if (ver == 1) {
        const QString saltHex = db().getDbSetting(DB_PIN_SALT_KEY);
        if (saltHex.isEmpty()) {
            qWarning() << "verifyPin: v1 credential but no salt found — corrupt record.";
            return false;
        }
        const QByteArray salt = QByteArray::fromHex(saltHex.toLatin1());
        verified = (db().getDbSetting(DB_PIN_HASH_KEY) == hashPin(pin, salt));
    } else {
        qWarning() << "verifyPin: unknown credential_version" << ver << "— access denied.";
        return false;
    }

    if (verified)
        clearFailedAttempts();
    else
        recordFailedAttempt();

    return verified;
}

bool isValidPassword(const QString& password) {
    return password.length() >= 6 && password.length() <= 20;
}

bool isValidPin(const QString& pin) {
    return isValidPassword(pin);
}

int credentialVersion() {
    return db().getDbSetting(DB_CRED_VER_KEY, "0").toInt();
}

bool setPin(const QString& password) {
    if (!isValidPassword(password)) return false;
    const QByteArray salt    = randomBytes(16);
    const QString    saltHex = QString::fromLatin1(salt.toHex());
    const QString    hash    = hashPin(password, salt);
    db().setDbSetting(DB_PIN_SALT_KEY, saltHex);
    db().setDbSetting(DB_PIN_HASH_KEY, hash);
    db().setDbSetting(DB_CRED_VER_KEY, QString::number(CURRENT_CREDENTIAL_VERSION));
    const bool ok = storeKey(deriveKey(password));
    if (ok) AuditLog::record(AuditLog::PIN_SET, "admin", 0, "Admin password set");
    return ok;
}

bool changePin(const QString& current, const QString& newPassword) {
    if (!verifyPin(current))           return false;
    if (!isValidPassword(newPassword)) return false;
    const QByteArray newSalt    = randomBytes(16);
    const QString    newSaltHex = QString::fromLatin1(newSalt.toHex());
    const QString    newHash    = hashPin(newPassword, newSalt);
    db().setDbSetting(DB_PIN_SALT_KEY, newSaltHex);
    db().setDbSetting(DB_PIN_HASH_KEY, newHash);
    db().setDbSetting(DB_CRED_VER_KEY, QString::number(CURRENT_CREDENTIAL_VERSION));
    const bool ok2 = storeKey(deriveKey(newPassword));
    if (ok2) AuditLog::record(AuditLog::PIN_CHANGED, "admin", 0, "Admin password changed");
    return ok2;
}

bool removePin(const QString& currentPin) {
    if (!verifyPin(currentPin)) return false;
    db().removeDbSetting(DB_PIN_HASH_KEY);
    db().removeDbSetting(DB_PIN_SALT_KEY);
    db().removeDbSetting(DB_CRED_VER_KEY);
    // Keep recovery_token_hash and install_secret — if admin sets a new PIN,
    // the same recovery file will still work (install_secret unchanged).
    deleteKey();
    AuditLog::record(AuditLog::PIN_REMOVED, "admin", 0, "Admin password removed");
    return true;
}

bool forceRemovePin() {
    db().removeDbSetting(DB_PIN_HASH_KEY);
    db().removeDbSetting(DB_PIN_SALT_KEY);
    db().removeDbSetting(DB_CRED_VER_KEY);
    db().removeDbSetting(DB_RECOVERY_HASH_KEY);
    // Clear rate limiter — fresh start after bypass
    db().removeDbSetting(DB_FAILED_ATTEMPTS);
    db().removeDbSetting(DB_LOCKOUT_UNTIL);
    deleteKey();
    AuditLog::record(AuditLog::BYPASS_USED, "admin", 0, "Admin password removed via bypass key");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Recovery file
// ═══════════════════════════════════════════════════════════════════════════

// Read install_secret from DB and derive the recovery token via HMAC.
static QByteArray deriveRecoveryToken() {
    const QString secretHex = db().getDbSetting("install_secret");
    if (secretHex.isEmpty()) {
        qWarning() << "deriveRecoveryToken: install_secret missing from DB.";
        return {};
    }
    const QByteArray secret = QByteArray::fromHex(secretHex.toLatin1());
    return QMessageAuthenticationCode::hash(
        QByteArray(RECOVERY_HMAC_CONTEXT),
        secret,
        QCryptographicHash::Sha256
    );
}

bool hasRecoveryFile() {
    return db().hasDbSetting(DB_RECOVERY_HASH_KEY);
}

QByteArray generateRecoveryFileData() {
    const QByteArray token = deriveRecoveryToken();
    if (token.isEmpty()) return {};

    // Store SHA-256(token) in DB — the token itself never lives in the DB
    const QString tokenHash = QString::fromLatin1(
        QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex());
    db().setDbSetting(DB_RECOVERY_HASH_KEY, tokenHash);

    return RecoveryFileHelper::build(token);
}

QString recoveryFileName() {
    return RecoveryFileHelper::suggestedFileName();
}

bool verifyRecoveryFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "verifyRecoveryFile: cannot open" << filePath;
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();

    const QByteArray token = RecoveryFileHelper::parse(data);
    if (token.isEmpty()) return false;

    const QString stored = db().getDbSetting(DB_RECOVERY_HASH_KEY);
    if (stored.isEmpty()) {
        qWarning() << "verifyRecoveryFile: no recovery hash stored in DB.";
        return false;
    }

    const QString computed = QString::fromLatin1(
        QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex());
    return stored == computed;
}

void rotateInstallSecret() {
    // Called after a successful --bypass-key.
    // Generates a new install_secret, invalidating the old recovery file.
    // Admin must generate a new recovery file after setting a new PIN.
    const QByteArray newSecret = randomBytes(32);
    db().setDbSetting("install_secret",
        QString::fromLatin1(newSecret.toHex()));
    db().removeDbSetting(DB_RECOVERY_HASH_KEY);
    qDebug() << "[bypass-key] install_secret rotated. Old recovery file is now invalid.";
}

// ═══════════════════════════════════════════════════════════════════════════
// Rate limiter
// ═══════════════════════════════════════════════════════════════════════════
//
// Lockout schedule (exponential backoff):
//   1–4 failures  : no lockout, attempt allowed
//   5  failures   : 30 seconds
//   6  failures   : 2 minutes
//   7  failures   : 10 minutes
//   8  failures   : 30 minutes
//   9+ failures   : 2 hours
//
// Attempting while locked resets the lockout clock (extending the wait).
// Resets fully on successful verification.

static int lockoutSecondsForAttempts(int attempts) {
    if (attempts < 5)  return 0;
    if (attempts == 5) return 30;
    if (attempts == 6) return 2   * 60;
    if (attempts == 7) return 10  * 60;
    if (attempts == 8) return 30  * 60;
    return                      2 * 60 * 60;  // 9+
}

int getLockoutSeconds() {
    const QString until = db().getDbSetting(DB_LOCKOUT_UNTIL);
    if (until.isEmpty()) return 0;
    const QDateTime lockoutUntil = QDateTime::fromString(until, Qt::ISODate);
    if (!lockoutUntil.isValid()) return 0;
    const qint64 remaining = QDateTime::currentDateTime().secsTo(lockoutUntil);
    return remaining > 0 ? static_cast<int>(remaining) : 0;
}

int getFailedAttempts() {
    return db().getDbSetting(DB_FAILED_ATTEMPTS, "0").toInt();
}

void recordFailedAttempt() {
    const int attempts = getFailedAttempts() + 1;
    db().setDbSetting(DB_FAILED_ATTEMPTS, QString::number(attempts));

    const int seconds = lockoutSecondsForAttempts(attempts);
    if (seconds > 0) {
        const QString until = QDateTime::currentDateTime()
                                  .addSecs(seconds)
                                  .toString(Qt::ISODate);
        db().setDbSetting(DB_LOCKOUT_UNTIL, until);
        qWarning() << "Auth: lockout triggered after" << attempts
                   << "failed attempts —" << seconds << "seconds.";
    }
}

void clearFailedAttempts() {
    db().removeDbSetting(DB_FAILED_ATTEMPTS);
    db().removeDbSetting(DB_LOCKOUT_UNTIL);
}

// ═══════════════════════════════════════════════════════════════════════════
// Bypass audit record
// ═══════════════════════════════════════════════════════════════════════════

void setBypassTimestamp() {
    db().setDbSetting(DB_BYPASS_TS_KEY,
        QDateTime::currentDateTime().toString(Qt::ISODate));
}

QString getBypassTimestamp() {
    return db().getDbSetting(DB_BYPASS_TS_KEY);
}

void setBypassPinWasSet(bool value) {
    db().setDbSetting(DB_BYPASS_SET_KEY, value ? "1" : "0");
}

bool bypassPinWasSet() {
    return db().getDbSetting(DB_BYPASS_SET_KEY, "0") == "1";
}

void clearBypassRecord() {
    db().removeDbSetting(DB_BYPASS_TS_KEY);
    db().removeDbSetting(DB_BYPASS_SET_KEY);
}

} // namespace PinManager
