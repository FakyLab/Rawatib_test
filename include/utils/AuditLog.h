#pragma once
#include <QString>

// ── AuditLog ──────────────────────────────────────────────────────────────
//
// Tamper-evident append-only log of all sensitive actions in the app.
// Stored in the audit_log table inside the encrypted database.
//
// Each entry includes a chain_hash = SHA256(prevHash + content), forming
// a hash chain. Any deletion or modification of an entry breaks the chain
// and is detected by verify().
//
// Call record() after every successful sensitive action.
// Call verify() from the AuditLogDialog to check integrity.

namespace AuditLog {

// ── Action constants ──────────────────────────────────────────────────────
constexpr const char* ADD_EMPLOYEE      = "add_employee";
constexpr const char* EDIT_EMPLOYEE     = "edit_employee";
constexpr const char* DELETE_EMPLOYEE   = "delete_employee";
constexpr const char* ADD_ATTENDANCE    = "add_attendance";
constexpr const char* EDIT_ATTENDANCE   = "edit_attendance";
constexpr const char* DELETE_ATTENDANCE = "delete_attendance";
constexpr const char* MARK_PAID         = "mark_paid";
constexpr const char* MARK_UNPAID       = "mark_unpaid";
constexpr const char* PAY_MONTH         = "pay_month";
constexpr const char* IMPORT            = "import";
constexpr const char* PIN_SET           = "pin_set";
constexpr const char* PIN_CHANGED       = "pin_changed";
constexpr const char* PIN_REMOVED       = "pin_removed";
constexpr const char* BYPASS_USED       = "bypass_used";
constexpr const char* LOCK_POLICY       = "lock_policy";
constexpr const char* BACKUP            = "backup";
constexpr const char* RESTORE           = "restore";

// ── API ───────────────────────────────────────────────────────────────────

// Record a log entry. Computes and stores the chain hash automatically.
void record(const QString& action,
            const QString& entity   = {},
            int            entityId = 0,
            const QString& detail   = {},
            const QString& oldValue = {},
            const QString& newValue = {});

// Verify chain integrity. Returns true if the log is intact.
// If broken, brokenAtId is set to the id of the first broken entry.
// If intact, brokenAtId is set to -1.
bool verify(int& brokenAtId);

} // namespace AuditLog
