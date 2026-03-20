#pragma once
#include <QString>
#include <QByteArray>

// ── EmployeePinManager ─────────────────────────────────────────────────────
//
// Per-employee PIN management for two self-service features:
//   1. Mark Paid    — employee marks their own records as paid
//   2. Kiosk PIN    — employee checks in / checks out using their PIN
//
// PINs are numeric, 6–12 digits, stored as PBKDF2-SHA256 hash + random salt
// in the employees table (pin_hash, pin_salt columns).
//
// Weak PINs (all same digit, simple ascending/descending sequences) are
// rejected by isValidPin() to discourage trivially guessable choices.
//
// PIN uniqueness is NOT enforced — storing hashes makes cross-employee
// comparison expensive and leaking "PIN already taken" creates an
// enumeration oracle. Choosing a private PIN is the employee's responsibility.

namespace EmployeePinManager {

// ── Validation ────────────────────────────────────────────────────────────
// Returns true if pin is 6–12 digits and not a trivially weak sequence.
bool isValidPin(const QString& pin);

// ── Hashing ───────────────────────────────────────────────────────────────
QByteArray generateSalt();
QString    hashPin(const QString& pin, const QByteArray& salt);

// ── Verification ─────────────────────────────────────────────────────────
// Returns true if pin matches the stored hash/salt for the given employee.
bool verifyPin(int employeeId, const QString& pin);

// ── Feature toggles (persisted in QSettings) ─────────────────────────────

// Mark Paid self-service toggle
bool isFeatureEnabled();
void setFeatureEnabled(bool enabled);

// Check-in / Check-out kiosk PIN toggle
bool isKioskPinEnabled();
void setKioskPinEnabled(bool enabled);

} // namespace EmployeePinManager
