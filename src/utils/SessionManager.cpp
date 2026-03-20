#include "utils/SessionManager.h"
#include <QByteArray>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>

namespace SessionManager {

// ── Session key ───────────────────────────────────────────────────────────
// Generated once at process startup, lives only in memory, never persisted.
// 32 bytes of cryptographic random data.

static QByteArray s_sessionKey;
static QByteArray s_token;       // non-empty = unlocked, empty = locked

static const QByteArray UNLOCK_CONTEXT = QByteArrayLiteral("rawatib_admin_unlocked_v1");

// ── Helpers ───────────────────────────────────────────────────────────────

static QByteArray computeToken() {
    return QMessageAuthenticationCode::hash(
        UNLOCK_CONTEXT, s_sessionKey, QCryptographicHash::Sha256);
}

// ── Public API ────────────────────────────────────────────────────────────

void init() {
    if (!s_sessionKey.isEmpty()) return;  // already initialized

    s_sessionKey.resize(32);
    auto rng = QRandomGenerator::securelySeeded();
    rng.generate(reinterpret_cast<quint32*>(s_sessionKey.data()),
                 reinterpret_cast<quint32*>(s_sessionKey.data())
                     + s_sessionKey.size() / sizeof(quint32));

    s_token.clear();  // start locked
    qDebug() << "SessionManager: session key initialized.";
}

void setUnlocked(bool unlocked) {
    if (s_sessionKey.isEmpty()) {
        qWarning() << "SessionManager::setUnlocked called before init() — calling init() now.";
        init();
    }
    if (unlocked) {
        s_token = computeToken();
    } else {
        // Overwrite token bytes before clearing — best-effort memory hygiene
        s_token.fill(0);
        s_token.clear();
    }
}

bool isUnlocked() {
    if (s_sessionKey.isEmpty() || s_token.isEmpty()) return false;
    // Constant-time comparison to resist timing attacks
    const QByteArray expected = computeToken();
    if (expected.size() != s_token.size()) return false;
    quint8 diff = 0;
    for (int i = 0; i < expected.size(); ++i)
        diff |= static_cast<quint8>(expected[i]) ^ static_cast<quint8>(s_token[i]);
    return diff == 0;
}

} // namespace SessionManager
