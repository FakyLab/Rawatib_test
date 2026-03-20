#include "database/Migrations.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// ── Development-era migration history ────────────────────────────────────
//
// The following schema changes were made during development (pre-v1.0).
// They are NOT implemented as versioned migrations because no production
// databases exist at these earlier states. All are included in the base
// schema that every fresh v1.0 install creates.
//
// History (for reference only):
//
// [dev] attendance.check_out changed from NOT NULL to DEFAULT NULL
//       to support open (check-in only) records.
//
// [dev] payroll_rules table added for deduction/addition rules.
//
// [dev] app_settings table added; PIN storage moved from QSettings
//       registry to encrypted DB (install_secret generated here).
//
// [dev] employees.pin_hash, employees.pin_salt columns added
//       for per-employee PIN authentication.
//
// ── Post-ship migrations start at version 1 ──────────────────────────────
//
// To add a migration:
//   1. Write a static void migrate_N(QSqlDatabase& db) function below.
//   2. Add  if (version < N) migrate_N(db);  to Migrations::run().
//   3. Increment CURRENT_VERSION in Migrations.h to N.
//   4. Update DatabaseManager::createTables() base schema if needed.

// ── Migration functions ───────────────────────────────────────────────────
// (none yet — post-ship migrations will be added here as migrate_2,
//  migrate_3, etc. when the schema needs to change after v1.0 ships)

// ── Runner ────────────────────────────────────────────────────────────────

namespace Migrations {

void run(QSqlDatabase& db) {
    // Read current schema version
    int version = 0;
    {
        QSqlQuery q(db);
        if (q.exec("PRAGMA user_version") && q.next())
            version = q.value(0).toInt();
    }

    if (version >= CURRENT_VERSION) {
        qDebug() << "Migrations: schema is up to date (version" << version << ")";
        return;
    }

    qDebug() << "Migrations: upgrading schema from version"
             << version << "to" << CURRENT_VERSION;

    // ── Apply pending migrations in order ─────────────────────────────────
    // (no migrations needed yet — fresh installs start at version 1)
    // Future example:
    //   if (version < 2) migrate_2(db);
    //   if (version < 3) migrate_3(db);

    // Write new version
    QSqlQuery setVersion(db);
    if (!setVersion.exec(QString("PRAGMA user_version = %1")
                             .arg(CURRENT_VERSION))) {
        qWarning() << "Migrations: failed to set user_version:"
                   << setVersion.lastError().text();
        return;
    }

    qDebug() << "Migrations: schema upgraded to version" << CURRENT_VERSION;
}

} // namespace Migrations
