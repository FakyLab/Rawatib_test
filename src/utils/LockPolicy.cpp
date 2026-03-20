#include "utils/LockPolicy.h"
#include "database/DatabaseManager.h"
#include <QHash>

namespace LockPolicy {

// ── Default table ──────────────────────────────────────────────────────────
//
// true  = locked by default  (mirrors original hardcoded behaviour)
// false = unlocked by default (CheckIn/CheckOut: kiosk-friendly default)
//
// Defaults are expressed as fallback values in getDbSetting() — they are
// never written to the DB until the admin explicitly changes a setting.
// This means a fresh install behaves identically to before without any
// DB writes at startup.

static const QHash<Feature, bool>& defaults() {
    static const QHash<Feature, bool> d = {
        { Feature::AddEmployee,      true  },
        { Feature::EditEmployee,     true  },
        { Feature::DeleteEmployee,   true  },
        { Feature::AddAttendance,    true  },
        { Feature::EditAttendance,   true  },
        { Feature::DeleteAttendance, true  },
        { Feature::MarkPaid,         true  },
        { Feature::MarkUnpaid,       true  },
        { Feature::CheckIn,          false },
        { Feature::CheckOut,         false },
        { Feature::ImportAttendance, true  },
        { Feature::BackupDatabase,   true  },
        { Feature::RestoreDatabase,  true  },
        { Feature::PayrollRules,     true  },
        { Feature::HideWages,        false },
    };
    return d;
}

// ── DB key ────────────────────────────────────────────────────────────────
// Stored in app_settings inside the encrypted database.
// Same key strings as before — only the storage backend changed.

static QString keyFor(Feature f) {
    switch (f) {
        case Feature::AddEmployee:      return QStringLiteral("lockpolicy/add_employee");
        case Feature::EditEmployee:     return QStringLiteral("lockpolicy/edit_employee");
        case Feature::DeleteEmployee:   return QStringLiteral("lockpolicy/delete_employee");
        case Feature::AddAttendance:    return QStringLiteral("lockpolicy/add_attendance");
        case Feature::EditAttendance:   return QStringLiteral("lockpolicy/edit_attendance");
        case Feature::DeleteAttendance: return QStringLiteral("lockpolicy/delete_attendance");
        case Feature::MarkPaid:         return QStringLiteral("lockpolicy/mark_paid");
        case Feature::MarkUnpaid:       return QStringLiteral("lockpolicy/mark_unpaid");
        case Feature::CheckIn:          return QStringLiteral("lockpolicy/check_in");
        case Feature::CheckOut:         return QStringLiteral("lockpolicy/check_out");
        case Feature::ImportAttendance: return QStringLiteral("lockpolicy/import_attendance");
        case Feature::BackupDatabase:   return QStringLiteral("lockpolicy/backup_database");
        case Feature::RestoreDatabase:  return QStringLiteral("lockpolicy/restore_database");
        case Feature::PayrollRules:     return QStringLiteral("lockpolicy/payroll_rules");
        case Feature::HideWages:        return QStringLiteral("lockpolicy/hide_wages");
    }
    return {};
}

// ── Public API ─────────────────────────────────────────────────────────────

bool isLocked(Feature f) {
    const QString key = keyFor(f);
    if (key.isEmpty()) return true;
    const QString def = defaults().value(f, true) ? QStringLiteral("true")
                                                   : QStringLiteral("false");
    return DatabaseManager::instance().getDbSetting(key, def) == QStringLiteral("true");
}

void setLocked(Feature f, bool locked) {
    const QString key = keyFor(f);
    if (key.isEmpty()) return;
    DatabaseManager::instance().setDbSetting(
        key, locked ? QStringLiteral("true") : QStringLiteral("false"));
}

void resetToDefaults() {
    for (auto it = defaults().constBegin(); it != defaults().constEnd(); ++it)
        DatabaseManager::instance().setDbSetting(
            keyFor(it.key()),
            it.value() ? QStringLiteral("true") : QStringLiteral("false"));
}

} // namespace LockPolicy
