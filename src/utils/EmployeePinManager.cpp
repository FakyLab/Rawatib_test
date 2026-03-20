#include "utils/EmployeePinManager.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QVariant>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

namespace EmployeePinManager {

// ── Constants ─────────────────────────────────────────────────────────────

static constexpr int PBKDF2_ITERATIONS   = 100000;
static constexpr int SALT_BYTES          = 16;
// Stored in app_settings (encrypted DB) — not QSettings/registry
static const char*   KEY_PAID_FEATURE    = "lockpolicy/employee_pin_feature";
static const char*   KEY_KIOSK_FEATURE   = "lockpolicy/employee_kiosk_pin_feature";

// ── Validation ────────────────────────────────────────────────────────────

bool isValidPin(const QString& pin) {
    if (pin.length() < 6 || pin.length() > 12) return false;
    for (const QChar& c : pin)
        if (!c.isDigit()) return false;

    // Reject all-same-digit: 111111, 999999, etc.
    bool allSame = true;
    for (int i = 1; i < pin.length(); ++i)
        if (pin[i] != pin[0]) { allSame = false; break; }
    if (allSame) return false;

    // Reject simple ascending sequence: 123456, 1234567, etc.
    bool ascending = true;
    for (int i = 1; i < pin.length(); ++i)
        if (pin[i].digitValue() != pin[i-1].digitValue() + 1) { ascending = false; break; }
    if (ascending) return false;

    // Reject simple descending sequence: 654321, 987654, etc.
    bool descending = true;
    for (int i = 1; i < pin.length(); ++i)
        if (pin[i].digitValue() != pin[i-1].digitValue() - 1) { descending = false; break; }
    if (descending) return false;

    return true;
}

// ── Hashing ───────────────────────────────────────────────────────────────

QByteArray generateSalt() {
    QByteArray salt(SALT_BYTES, Qt::Uninitialized);
    auto rng = QRandomGenerator::securelySeeded();
    rng.generate(reinterpret_cast<quint32*>(salt.data()),
                 reinterpret_cast<quint32*>(salt.data()) + SALT_BYTES / sizeof(quint32));
    return salt;
}

QString hashPin(const QString& pin, const QByteArray& salt) {
    const QByteArray password = pin.toUtf8();
    QByteArray block = salt + QByteArray("\x00\x00\x00\x01", 4);

    QByteArray u = QMessageAuthenticationCode::hash(
        block, password, QCryptographicHash::Sha256);
    QByteArray result = u;

    for (int i = 1; i < PBKDF2_ITERATIONS; ++i) {
        u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha256);
        for (int j = 0; j < result.size(); ++j)
            result[j] = result[j] ^ u[j];
    }
    return QString::fromLatin1(result.toHex());
}

// ── Verification ─────────────────────────────────────────────────────────

bool verifyPin(int employeeId, const QString& pin) {
    if (pin.length() < 6 || pin.length() > 12) return false;

    auto& db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT pin_hash, pin_salt FROM employees WHERE id=:id");
    q.bindValue(":id", employeeId);

    if (!q.exec() || !q.next()) return false;

    const QString storedHash = q.value(0).toString();
    const QString storedSalt = q.value(1).toString();
    if (storedHash.isEmpty() || storedSalt.isEmpty()) return false;

    const QByteArray salt     = QByteArray::fromHex(storedSalt.toLatin1());
    const QString    computed = hashPin(pin, salt);

    // Constant-time comparison
    if (computed.length() != storedHash.length()) return false;
    int diff = 0;
    for (int i = 0; i < computed.length(); ++i)
        diff |= (computed[i].unicode() ^ storedHash[i].unicode());
    return diff == 0;
}

// ── Feature toggles ───────────────────────────────────────────────────────
// Stored in app_settings inside the encrypted database — not the registry.
// Defaults to false (feature disabled) if the key doesn't exist yet.

bool isFeatureEnabled() {
    return DatabaseManager::instance().getDbSetting(KEY_PAID_FEATURE, "false") == "true";
}

void setFeatureEnabled(bool enabled) {
    DatabaseManager::instance().setDbSetting(KEY_PAID_FEATURE,
        enabled ? "true" : "false");
}

bool isKioskPinEnabled() {
    return DatabaseManager::instance().getDbSetting(KEY_KIOSK_FEATURE, "false") == "true";
}

void setKioskPinEnabled(bool enabled) {
    DatabaseManager::instance().setDbSetting(KEY_KIOSK_FEATURE,
        enabled ? "true" : "false");
}

} // namespace EmployeePinManager
