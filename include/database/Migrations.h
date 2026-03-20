#pragma once
#include <QSqlDatabase>

// ── Migrations ────────────────────────────────────────────────────────────
//
// Schema versioning via SQLite's built-in PRAGMA user_version.
//
// On every app launch, Migrations::run() is called once after the database
// opens. It reads the current user_version, applies any pending migrations
// in order, then writes the new version back.
//
// Rules:
//   - Never modify an existing migrate_N() function after it has shipped.
//   - Every schema change after v1.0 ships gets its own migrate_N().
//   - Increment CURRENT_VERSION each time a new migration is added.
//   - Fresh installs set user_version = CURRENT_VERSION directly in
//     DatabaseManager::createTables() — they never run any migrations.

namespace Migrations {

constexpr int CURRENT_VERSION = 1;

// Run all pending migrations for the given database.
// Called once by DatabaseManager after the DB opens and tables exist.
void run(QSqlDatabase& db);

} // namespace Migrations
