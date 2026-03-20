#pragma once
#include <QString>
#include <QByteArray>

namespace PinManager {

// ── Password storage (PBKDF2-SHA256 hash in encrypted DB) ────────────────
// The PIN hash, random salt, and bypass audit trail are stored in the
// app_settings table inside the encrypted SQLCipher database — not in
// the registry. This prevents bypassing the admin lock by editing the registry.
//
// Hashing uses PBKDF2-SHA256 with 100,000 iterations and a random 16-byte
// salt generated at PIN-set time. The salt is stored as admin_pin_salt.
//
// IMPORTANT: all functions below (except loadKey/storeKey/deleteKey/deriveKey)
// require DatabaseManager::initialize() to have succeeded first.
bool    isPinSet();
bool    verifyPin(const QString& password);
bool    setPin(const QString& password);
bool    changePin(const QString& current, const QString& newPassword);
bool    removePin(const QString& current);
bool    forceRemovePin();

bool    isValidPassword(const QString& password);
bool    isValidPin(const QString& pin);

// Returns the credential_version stored in the DB (0 if not set).
int     credentialVersion();

// ── Rate limiting ─────────────────────────────────────────────────────────
// Protects against brute-force attacks on the admin password.
// State is stored in app_settings inside the encrypted DB.

// Returns seconds remaining in lockout (0 = not locked out).
int     getLockoutSeconds();

// Returns current failed attempt count.
int     getFailedAttempts();

// Records a failed attempt and applies lockout if threshold reached.
void    recordFailedAttempt();

// Resets failed attempt counter and clears any lockout (call on success).
void    clearFailedAttempts();

// ── SQLCipher key management (PBKDF2 + OS keychain) ──────────────────────
// Safe to call before DatabaseManager::initialize().
QByteArray  deriveKey(const QString& pin);
bool        storeKey(const QByteArray& key);
QByteArray  loadKey();
void        deleteKey();

// ── Recovery file (--bypass-key support) ─────────────────────────────────
// Derived from install_secret in the encrypted DB.
// Valid across PIN changes, app reinstalls, and OS reinstalls (same DB).
// Invalidated only after a successful --bypass-key (rotateInstallSecret).

bool        hasRecoveryFile();
QByteArray  generateRecoveryFileData();
QString     recoveryFileName();
bool        verifyRecoveryFile(const QString& filePath);
void        rotateInstallSecret();

// ── Bypass audit record (stored in encrypted DB) ─────────────────────────
void    setBypassTimestamp();
QString getBypassTimestamp();
void    setBypassPinWasSet(bool value);
bool    bypassPinWasSet();
void    clearBypassRecord();

} // namespace PinManager
