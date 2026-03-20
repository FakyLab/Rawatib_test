#pragma once

// ── LockPolicy ─────────────────────────────────────────────────────────────
//
// Controls which operations require the admin lock to be open.
// Defaults mirror the original hardcoded behaviour, so existing users
// see no change on first launch after upgrading.
//
// Persisted in app_settings (encrypted DB) under the "lockpolicy/" key prefix.
// Safe to call from any thread after QApplication is constructed.
//
// NOTE: A few operations are intentionally NOT in this enum and remain
// unconditionally protected regardless of policy:
//   - Generate Recovery File  (always re-verifies password)
//   - Set / Change / Remove admin password  (always require unlock)
//   - The Lock Policy dialog itself  (always requires unlock to open)

namespace LockPolicy {

enum class Feature {
    // ── Employee Management ────────────────────────────────────────────────
    AddEmployee,
    EditEmployee,
    DeleteEmployee,

    // ── Attendance ─────────────────────────────────────────────────────────
    AddAttendance,
    EditAttendance,
    DeleteAttendance,
    MarkPaid,
    MarkUnpaid,
    CheckIn,
    CheckOut,

    // ── Data & Settings ────────────────────────────────────────────────────
    ImportAttendance,
    BackupDatabase,
    RestoreDatabase,
    PayrollRules,

    // ── Visibility ─────────────────────────────────────────────────────────
    HideWages,
};

// Returns true if the given feature requires admin unlock.
// When false, the operation proceeds without checking the lock state.
bool isLocked(Feature f);

// Persist a single feature's lock state.
void setLocked(Feature f, bool locked);

// Restore all features to their factory defaults.
void resetToDefaults();

} // namespace LockPolicy
