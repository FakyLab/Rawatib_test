# Rawatib — Architecture Guide

**Version:** 1.0.0  
**Stack:** Qt 6 · C++17 · SQLCipher · QtKeychain · QXlsx  
**Platform:** Windows, macOS, Linux (cross-platform desktop)

This document explains how Rawatib is built — the major components, how they interact, the key design decisions, and the reasoning behind them. It is intended for contributors who want to understand the codebase before making changes.

---

## Table of Contents

1. [High-Level Overview](#1-high-level-overview)
2. [Directory Structure](#2-directory-structure)
3. [Data Model](#3-data-model)
4. [Database Layer](#4-database-layer)
5. [Repository Layer](#5-repository-layer)
6. [Calculation Engine](#6-calculation-engine)
7. [UI Architecture](#7-ui-architecture)
8. [Security Architecture](#8-security-architecture)
9. [Lock & Access Control System](#9-lock--access-control-system)
10. [Translation & Internationalisation](#10-translation--internationalisation)
11. [Import & Export](#11-import--export)
12. [Printing](#12-printing)
13. [Settings & Persistence](#13-settings--persistence)
14. [Startup Sequence](#14-startup-sequence)
15. [Adding a New Language](#15-adding-a-new-language)
16. [Adding a New Currency](#16-adding-a-new-currency)
17. [Schema Migrations](#17-schema-migrations)
18. [Known Constraints & Design Decisions](#18-known-constraints--design-decisions)

---

## 1. High-Level Overview

Rawatib is a single-user desktop payroll and attendance manager targeting small businesses in Arabic-speaking and other markets. It has no server, no network dependency, and no cloud component — all data lives in a local encrypted SQLite database.

The application follows a layered architecture:

```
┌─────────────────────────────────────────────────────┐
│                     UI Layer                        │
│  MainWindow · EmployeePanel · AttendanceTab         │
│  SalaryTab · AdvancedTab · Dialogs                  │
├─────────────────────────────────────────────────────┤
│                  Utility Layer                      │
│  PayrollCalculator · DeductionPolicy · LockPolicy   │
│  ImportHelper · ExportHelper · PrintHelper          │
│  PinManager · SessionManager · AuditLog             │
│  CurrencyManager · LanguageRegistry · UpdateChecker │
├─────────────────────────────────────────────────────┤
│                 Repository Layer                    │
│  EmployeeRepository · AttendanceRepository          │
│  PayrollRulesRepository · DayExceptionRepository    │
├─────────────────────────────────────────────────────┤
│                  Database Layer                     │
│  DatabaseManager · Migrations · AutoBackupManager   │
│  SQLCipher (encrypted SQLite)                       │
└─────────────────────────────────────────────────────┘
```

The UI layer never touches the database directly — it always goes through repositories. Repositories never perform calculations — they only read and write raw data. Calculations live in dedicated stateless utility namespaces.

---

## 2. Directory Structure

```
project/
├── src/
│   ├── main.cpp                    # App entry point, translator loading, DB init
│   ├── database/
│   │   ├── DatabaseManager.cpp     # SQLCipher connection, key management, schema
│   │   ├── Migrations.cpp          # Post-ship schema versioning
│   │   └── AutoBackupManager.cpp   # Startup backup rotation
│   ├── models/
│   │   ├── Employee.cpp            # Employee struct + wage helpers
│   │   ├── AttendanceRecord.cpp    # Record struct + calculate()/calculateMonthly()
│   │   └── PayrollRule.cpp         # Payroll rule struct
│   ├── repositories/
│   │   ├── EmployeeRepository.cpp
│   │   ├── AttendanceRepository.cpp
│   │   ├── PayrollRulesRepository.cpp
│   │   └── DayExceptionRepository.cpp
│   ├── ui/
│   │   ├── MainWindow.cpp          # Root window, menu bar, session management
│   │   ├── EmployeePanel.cpp       # Left-side employee list
│   │   ├── AttendanceTab.cpp       # Daily records tree, check-in/out
│   │   ├── SalaryTab.cpp           # Monthly summary, net pay
│   │   ├── AdvancedTab.cpp         # Payroll rules, deduction policy, exceptions
│   │   └── dialogs/                # All modal dialogs
│   └── utils/
│       ├── PayrollCalculator.cpp   # Pure gross→net calculation
│       ├── DeductionPolicy.cpp     # Company-wide deduction mode
│       ├── LockPolicy.cpp          # Per-feature admin lock settings
│       ├── PinManager.cpp          # Admin PIN hash, SQLCipher key, recovery
│       ├── SessionManager.cpp      # HMAC-based unlock state
│       ├── EmployeePinManager.cpp  # Per-employee PIN for kiosk/self-service
│       ├── AuditLog.cpp            # Tamper-evident hash-chain log
│       ├── ImportHelper.cpp        # Two-pass CSV import
│       ├── ExportHelper.cpp        # XLSX/CSV export
│       ├── PrintHelper.cpp         # HTML-based print reports
│       ├── CurrencyManager.cpp     # Currency table, formatting
│       ├── LanguageRegistry.cpp    # Supported languages, RTL detection
│       ├── UpdateChecker.cpp       # GitHub Releases API — silent + manual update check
│       └── SettingsManager.cpp     # QSettings wrapper (non-sensitive prefs)
├── include/                        # Header files mirroring src/ structure
├── translations/                   # .ts files per language
├── third_party/
│   ├── sqlcipher/                  # SQLCipher amalgamation (sqlite3.c/h)
│   ├── qsqlcipher/                 # Qt SQL driver wrapping SQLCipher
│   ├── qtkeychain/                 # OS keychain access (submodule)
│   └── QXlsx/                      # XLSX read/write (submodule, optional)
├── CMakeLists.txt
└── resources.qrc
```

---

## 3. Data Model

### Employee

```cpp
struct Employee {
    int     id;
    QString name, phone, notes;
    PayType payType;          // Hourly or Monthly

    // Hourly fields
    double  hourlyWage;

    // Monthly fields
    double  monthlySalary;
    int     workingDaysPerMonth;   // denominator for daily rate (typically 26)
    QTime   expectedCheckin;       // e.g. 09:00
    QTime   expectedCheckout;      // e.g. 17:00
    int     lateToleranceMin;      // grace period before deducting lateness

    // Per-employee kiosk/self-service PIN (optional)
    QString pinHash, pinSalt;
};
```

Two pay types exist and are mutually exclusive:

- **Hourly** — `dailyWage = hoursWorked × hourlyWage`. Simple and direct.
- **Monthly** — `dailyWage = monthlySalary / workingDaysPerMonth − deductions`. Deductions are calculated per record using the company-wide `DeductionPolicy`.

Key helpers on `Employee`:
- `dailyRate()` — `monthlySalary / workingDaysPerMonth`
- `perMinuteRate()` — `dailyRate() / (workHoursPerDay × 60)`, used in PerMinute deduction mode

### AttendanceRecord

```cpp
struct AttendanceRecord {
    int     id, employeeId;
    QDate   date;
    QTime   checkIn, checkOut;   // checkOut invalid = open record (checked in, not yet out)
    double  hoursWorked, dailyWage;
    bool    paid;

    // Monthly transparency fields (always 0 for hourly)
    int     lateMinutes, earlyMinutes;
    double  baseDailyRate;        // full daily rate at record creation time
    double  dayDeduction;         // monetary sum of late+early penalties
    QString appliedDeductionMode; // "perminute", "perday", or "off"
};
```

The transparency fields (`baseDailyRate`, `dayDeduction`, `appliedDeductionMode`) are stored so that historical records remain accurate even if the employee's salary, schedule, or the company deduction policy changes later. The salary summary reads these stored values directly rather than recalculating.

Record calculation happens in two methods on `AttendanceRecord`:
- `calculate(hourlyWage)` — for hourly employees
- `calculateMonthly(...)` — for monthly employees; implements all three deduction modes

### PayrollRule

```cpp
struct PayrollRule {
    int     id;
    QString name;
    Type    type;       // Deduction or Addition
    Basis   basis;      // FixedAmount or PercentOfGross
    double  value;
    bool    enabled;
    int     sortOrder;
    AppliesTo appliesTo; // All, Monthly, or Hourly
};
```

Rules are global — they apply to all employees of the matching pay type. They are layered on top of gross pay in the `SalaryTab` to produce net pay. Users can override the value of any rule for the current view without changing the global default.

### DayException

```cpp
struct DayException {
    int     id;
    QDate   date;
    int     employeeId;   // 0 = company-wide (applies to all employees)
    QString reason;
};
```

Day exceptions are approved off-days that are not counted as absences for monthly salary employees. A company-wide exception (e.g. a public holiday) uses `employeeId = 0`.

---

## 4. Database Layer

### Always Encrypted

The database is **always encrypted with SQLCipher**, even when no admin PIN is set. When no PIN exists, a compiled-in fallback key derived from a fixed app-specific string is used. When a PIN is set, the database is rekeyed to a key derived from that PIN via PBKDF2-SHA256.

The fallback key is visible in open source. This is intentional and acceptable: it protects against casual file copying, not against an attacker who has read the source code and has full machine access. Real protection requires setting an admin PIN.

### Schema

Six tables, all created with `IF NOT EXISTS` on every launch:

| Table | Purpose |
|---|---|
| `employees` | Employee records including PIN hash/salt |
| `attendance` | Daily attendance records with all wage fields |
| `audit_log` | Tamper-evident hash-chain of all sensitive actions |
| `payroll_rules` | Global deduction/addition rules |
| `day_exceptions` | Approved off-days (company-wide or per-employee) |
| `app_settings` | Key-value store for security settings and lock policy |

The `app_settings` table stores everything that is security-sensitive: the admin PIN hash and salt, the install secret (used for recovery file derivation), lock policy per feature, deduction policy mode, rate-limiting state, and the bypass audit record. It deliberately lives inside the encrypted database rather than in `QSettings` (registry/plist), preventing bypass by editing system settings.

### Key Management Flow

```
Fresh install (no PIN):
  fallbackKey() → PRAGMA key → DB opens

PIN set:
  PRAGMA rekey from fallbackKey() → deriveKey(pin)
  Derived key stored in OS keychain via QtKeychain

Subsequent launches with PIN:
  PinManager::loadKey() from OS keychain → PRAGMA key → DB opens

Keychain missing (OS reinstall, machine migration):
  initialize() fails → DatabaseUnlockDialog shown
  Admin enters PIN → tryUnlockWithPin() → key stored in keychain again

Emergency bypass (--bypass-key):
  Old key captured from keychain before launch
  DB opened with captured key
  Recovery file verified against stored hash in DB
  PIN removed, DB rekeyed to fallbackKey()
```

### DatabaseManager

`DatabaseManager` is a singleton. It owns the single named `QSqlDatabase` connection (`"main_connection"`). All repositories access the DB through `DatabaseManager::instance().database()`.

Critical detail: after any `close()`/`open()` cycle (backup, restore), `reapplyKey()` must be called immediately — SQLCipher requires `PRAGMA key` to be the very first query on a new connection handle.

---

## 5. Repository Layer

All four repositories are singletons following the same pattern:

```cpp
class EmployeeRepository {
public:
    static EmployeeRepository& instance();
    bool addEmployee(Employee& employee);  // sets employee.id on success
    // ...
    QString lastError() const;
private:
    EmployeeRepository() = default;
    mutable QString m_lastError;
};
```

Repositories own all SQL. The UI never writes SQL — it calls repository methods. This makes it easy to find every place a given table is accessed.

### AttendanceRepository specifics

This is the most complex repository. Key points:

**Overlap detection** — `checkOverlap()` is called before every `addRecord()`, `updateRecord()`, `checkIn()`, and `checkOut()`. It handles three cases: completed vs completed (standard interval overlap), new open record vs existing sessions, and existing open session blocking new sessions that start after it.

**Monthly summary** — `getMonthlySummary()` computes everything the `SalaryTab` needs in a single trip to the database: total hours, total salary, paid/unpaid amounts, absent days, exception days, deduction breakdown, and mixed-mode detection. It does not recalculate wages — it reads the stored values.

**Deduction split in summary** — The summary shows `lateDeduction` and `earlyDeduction` separately for display purposes, but these are already baked into `totalSalary` via per-record `day_deduction`. They are never added to `totalDeductions` again. `totalDeductions` contains only `absentDeduction`.

---

## 6. Calculation Engine

### AttendanceRecord::calculateMonthly()

Called at record creation (adding a record or checking out). Implements three deduction modes:

- **PerMinute** — `deduction = (lateMinutes + earlyMinutes) × perMinuteRate`. Proportional and continuous. Default.
- **PerDay** — Fixed penalty per occurrence. If `lateMinutes > 0`: deduct `penaltyPct%` of `dailyBaseRate`. Same independently for `earlyMinutes > 0`. Max deduction per day = 2 × penalty (capped by the existing `qMax(0.0, ...)` guard).
- **Off** — Minutes are recorded for display but `dayDeduction = 0` always. Admin handles penalties manually via payroll rules.

All monetary results are rounded to 2 decimal places using `std::round(x * 100.0) / 100.0` to avoid floating-point noise.

### PayrollCalculator

A pure stateless namespace — no DB access, no Qt widgets. Takes gross pay and a list of `AppliedRule` objects, returns a `Result` containing gross pay, total additions, total deductions, net pay, and a per-rule breakdown.

```cpp
struct AppliedRule {
    PayrollRule rule;
    double      appliedValue;   // user may override this in SalaryTab
};

Result calculate(double grossPay, const QVector<AppliedRule>& appliedRules);
```

The `appliedValue` mechanism allows the admin to override a rule's value for the current session view (e.g. entering the actual bonus amount for this month) without changing the global rule default.

### DeductionPolicy

A namespace that reads and writes two keys in `app_settings`: `deduction_mode` and `perday_penalty_pct`. Called by `AttendanceRecord::calculateMonthly()` at record creation time. The mode in effect at record creation is stored in `applied_deduction_mode` — this matters for the mixed-mode detection in `getMonthlySummary()`.

---

## 7. UI Architecture

### Layout

```
MainWindow (QMainWindow)
├── EmployeePanel (left panel, fixed width 200–280px)
│   └── QListWidget + search + Add/Edit/Delete buttons
└── QTabWidget (right panel, stretches)
    ├── AttendanceTab
    │   └── Year/month selector + QTreeWidget + action buttons
    ├── SalaryTab
    │   └── Summary cards + payroll rules table + print/export
    └── AdvancedTab
        └── QTabWidget (All Employees | Monthly | Hourly)
            ├── Payroll rules tables per type
            ├── Day exceptions (All tab)
            └── Deduction policy (Monthly tab)
```

### Signal flow

The core UI coordination signal is `adminLockChanged(bool unlocked)` emitted by `MainWindow`. All child widgets connect to it via `onLockChanged(bool)` to update their visual state (lock icon, wage visibility, button enabled state).

The reverse path — a child widget performing an inline unlock — emits `adminUnlocked()` which `MainWindow` catches via `tryUnlockAdmin()` to sync its own state. **Critically**, the child widget must call `SessionManager::setUnlocked(true)` before emitting `adminUnlocked()`, otherwise `tryUnlockAdmin()` sees a locked `SessionManager` state and prompts for the password a second time.

Month selection is synchronised between `AttendanceTab` and `SalaryTab` through `MainWindow` as coordinator — the tab that changes month emits `monthChanged(int year, int month)`, `MainWindow` catches it and calls `setMonth()` on the other tab.

Self-view (employee viewing their own wages via their PIN) is also synchronised between the two tabs through `MainWindow`.

### Guard functions

Three guard patterns exist in `AttendanceTab`:

- `guardAdmin(Feature)` — checks LockPolicy, calls `SessionManager::isUnlocked()`, shows PIN dialog if needed
- `guardMarkPaid()` — three-state: MarkPaid unlocked → allow; employee kiosk PIN enabled + employee has PIN → ask employee PIN; otherwise → admin unlock
- `guardKioskPin(Feature)` — same three-state logic for CheckIn/CheckOut

`EmployeePanel::guardAdmin()` follows the same pattern as `AttendanceTab::guardAdmin()`.

---

## 8. Security Architecture

### Admin PIN

The admin PIN is stored as a PBKDF2-SHA256 hash with 100,000 iterations and a 16-byte random salt, persisted in `app_settings` inside the encrypted database. The PIN itself is never stored.

The same PIN also drives the SQLCipher key derivation: `deriveKey(pin)` runs PBKDF2-SHA256 with a different fixed salt to produce a 32-byte key that SQLCipher uses to encrypt the entire database file. This means PIN verification and database decryption are the same underlying operation.

### SQLCipher key chain

```
Admin PIN
    ↓  PBKDF2-SHA256 (100k iter, fixed cipher salt)
SQLCipher key (32 bytes)
    ↓  stored in OS keychain
      Windows: Windows Credential Manager
      macOS:   Keychain
      Linux:   libsecret / KWallet
```

On launch, `PinManager::loadKey()` retrieves the stored key from the OS keychain. If the keychain entry is missing (OS reinstall, machine migration, first run after PIN set), `DatabaseUnlockDialog` prompts for the PIN and re-derives the key.

### SessionManager

The admin unlock state is stored as an HMAC-SHA256 token rather than a boolean. A 32-byte random session key is generated at process startup and never leaves memory. The token is `HMAC-SHA256(session_key, "rawatib_admin_unlocked_v1")`.

`isUnlocked()` recomputes the expected token on every call and does a constant-time comparison. Flipping the `s_token` bytes in memory without knowing `s_sessionKey` produces a comparison failure. The local `m_adminUnlocked` boolean in UI widgets is kept only for rendering (icon emoji, wage masking) — all actual access guards call `SessionManager::isUnlocked()`.

### AuditLog

Every sensitive action is recorded with a hash chain: `chain_hash = SHA256(prevHash + timestamp + action + entity + entityId + detail + oldValue + newValue)`. The first entry uses `"GENESIS"` as `prevHash`.

`AuditLog::verify()` walks all entries in order and recomputes each hash. Any deletion or modification of a past entry breaks the chain and is detected. The audit log is stored inside the encrypted database, so it is also protected at rest.

### Recovery file

The recovery file enables the `--bypass-key` emergency access flow. It contains an HMAC-SHA256 token derived from `install_secret` (a random 32-byte value generated at database creation and stored in `app_settings`). The database stores only `SHA256(token)` — the token itself is never in the database.

The recovery file remains valid across PIN changes because `install_secret` does not change when the PIN changes. It is invalidated after a successful `--bypass-key` use by calling `rotateInstallSecret()`, which generates a new `install_secret`.

### Employee PINs

Per-employee PINs (6–12 digits) use the same PBKDF2-SHA256 approach as the admin PIN — hash + random salt stored in the `employees` table. They gate two self-service features: Mark Paid and kiosk Check-in/out. They are independent of the admin PIN and of each other.

Weak PINs (all same digit, simple sequences) are rejected by `EmployeePinManager::isValidPin()`.

### Rate limiting

Failed admin PIN attempts are counted in `app_settings`. After 5 failures, a lockout is applied with exponential backoff. The lockout state survives app restarts because it lives in the encrypted database.

---

## 9. Lock & Access Control System

`LockPolicy` controls which operations require the admin session to be unlocked. Settings are persisted per-feature in `app_settings` inside the encrypted database.

Default values mirror the original hardcoded behaviour — most operations require admin unlock, but Check-in and Check-out are unlocked by default (kiosk-friendly).

The `HideWages` feature is special: when enabled and the admin is locked, wage figures are replaced with `--` throughout the UI, in print reports, and in exports. Employees can override this for themselves via their personal PIN using the self-view feature.

Operations that are **always** protected regardless of LockPolicy:
- Setting, changing, or removing the admin password
- Generating a recovery file
- Opening the Lock Policy dialog itself

---

## 10. Translation & Internationalisation

### Language registry

`LanguageRegistry` is the single source of truth for supported languages. Adding a language requires one row in `s_languages` in `LanguageRegistry.cpp`:

```cpp
{ "es", "Español", false },  // code, native name, isRtl
```

The first-run dialog, the View → Language menu, and the translator loading in `main.cpp` all read from this registry automatically. No other code needs to change.

### Translation files

Each language has a `.ts` file in `translations/rawatib_<code>.ts`. CMake globs `rawatib_*.ts` automatically — dropping a new file into `translations/` is sufficient. Run `cmake ..` once after adding the file.

The `.ts` files are compiled to `.qm` by `lrelease` and embedded as Qt resources under the `:/i18n/` prefix.

### Qt base translations

Standard dialog buttons (OK, Cancel, etc.) are translated by Qt's own `qtbase_<code>.qm` files. These are not included in the repository — they must be copied from the Qt installation into `translations/` or will be found automatically at runtime on developer machines.

Note: Qt ships `qtbase_zh_CN.qm` for Simplified Chinese, not `qtbase_zh.qm`. The CMake build and the runtime loader both apply a mapping from the app's `zh` code to Qt's `zh_CN` filename. If adding other languages with locale-qualified qtbase names (e.g. `pt_BR`), add entries to both mappings.

### RTL support

`LanguageRegistry::isRtl()` drives layout direction. Set once at startup in `main.cpp` via `app.setLayoutDirection()`. The PrintHelper derives print direction from `QGuiApplication::layoutDirection()` and adjusts column order and text alignment accordingly. CSV exports always use English headers regardless of UI language to preserve import compatibility.

### CJK font stacks

`PrintHelper` has dedicated font stacks for Chinese, Japanese, and Korean (`sansStack()` and `serifStack()`). Without these, Qt's HTML renderer falls back to Latin fonts that have no CJK glyph coverage and produces blank boxes. The stacks are ordered: Windows system font → macOS system font → Noto (Linux).

---

## 11. Import & Export

### Two-pass CSV import

The import system uses a deliberate two-pass design to keep the preview dialog and the commit stage cleanly separated.

**Pass 1 — `parseFile()`**: Reads the entire CSV, classifies every record as `Clean`, `SoftConflict`, or `HardError`, resolves employee names against the database, detects wage mismatches, and computes checksums against the CSV summary block. No DB writes.

**Between passes — `ImportPreviewDialog`**: Shows the admin a row-level preview. The admin can deselect individual records, decide how to handle wage mismatches, and skip entire employee blocks. On a clean file, the preview is skipped and a simple confirmation dialog is shown instead.

**Pass 2 — `commitImport()`**: Wraps all DB writes in a single transaction. Inserts only the records the admin approved. If `addEmployee()` fails (DB-level error, not a data validation issue), the entire transaction is rolled back and a clear error is returned. Individual `addRecord()` failures (overlaps etc.) count as `skipped` and are reported in the summary — they do not abort the import.

The CSV format is the same format that `ExportHelper::exportMonth()` produces — the import and export are intentionally compatible, enabling round-trip data portability.

### Export

`ExportHelper` supports two formats:

- **XLSX** — via QXlsx (optional dependency). Full formatting, RTL support, Excel formulas for totals.
- **CSV** — always available, English column headers regardless of UI language.

Two export modes:
- `exportMonth()` — single employee, one month. Called from the SalaryTab Export button.
- `exportAll()` — all employees, one month or all time. Called from File → Export All.

---

## 12. Printing

`PrintHelper` generates HTML strings and prints them via `QTextDocument::print()`. Two paper size paths exist:

- **A4 (≥ 100mm width)** — full two-column layout with attendance table
- **Receipt (65–100mm)** — compact single-column with 8pt/7pt fonts
- **Narrow receipt (< 65mm)** — same as receipt but 7pt/6pt fonts

Three document types:
- **Monthly report** — summary + full daily attendance table
- **Payment slip** — amount paid, breakdown, signature lines
- **Receipt variants** of both above

HTML templates use injected font stacks and text-align values so they work correctly for both RTL and LTR languages without separate templates.

---

## 13. Settings & Persistence

Two separate persistence mechanisms:

| Mechanism | What it stores | Why |
|---|---|---|
| `QSettings` (registry/plist) | Language, session timeout, first-run flag | Non-sensitive, needs to be readable before DB opens |
| `app_settings` table in encrypted DB | Admin PIN hash/salt, lock policy, deduction policy, rate-limit state, bypass audit record | Security-sensitive — must be inside the encrypted DB |

`SettingsManager` wraps `QSettings`. `DatabaseManager::getDbSetting()`/`setDbSetting()` wraps `app_settings`. Code that needs a setting reads from whichever store is appropriate for its sensitivity level.

`CurrencyManager::setCurrent()` also uses `QSettings` — currency choice is non-sensitive.

---

## 14. Startup Sequence

```
main()
  │
  ├─ Parse command-line arguments (--dev-mode, --lang, --db-path, --bypass-key, etc.)
  │
  ├─ FirstRunDialog::showIfNeeded()    ← language + currency selection on first launch
  │
  ├─ Load Qt translator (qtbase_<lang>.qm)
  ├─ Load app translator (rawatib_<lang>.qm)
  ├─ Set layout direction (RTL/LTR)
  │
  ├─ Locate database path (AppDataLocation / --db-path override)
  ├─ Acquire single-instance lock file
  │
  ├─ Handle --reset / --reset-all      ← early exit paths
  │
  ├─ Capture keychain key if --bypass-key
  │
  ├─ DatabaseManager::initialize()
  │     ├─ Open SQLCipher connection
  │     ├─ Apply key (keychain → fallback → bypass override)
  │     ├─ Verify key (SELECT count(*) FROM sqlite_master)
  │     ├─ tryMigrateFromPlain()       ← one-time upgrade from older plain-SQLite builds
  │     ├─ enableForeignKeys()
  │     ├─ createTables()              ← IF NOT EXISTS, safe on every launch
  │     └─ Migrations::run()           ← apply any pending post-ship migrations
  │
  ├─ Handle DB key mismatch → DatabaseUnlockDialog (OS reinstall / machine migration)
  │
  ├─ Complete --bypass-key sequence if applicable
  │
  ├─ AutoBackupManager::runStartupBackup()
  │
  └─ MainWindow::show() → app.exec()
```

---

## 15. Adding a New Language

1. **Register the language** — add one row to `s_languages` in `src/utils/LanguageRegistry.cpp`:
   ```cpp
   { "es", "Español", false },
   ```
   Set `isRtl = true` only for right-to-left scripts (Arabic, Persian, Urdu, Hebrew).

2. **Create the `.ts` file** using `lupdate`:
   ```bash
   lupdate path/to/project -ts translations/rawatib_es.ts
   ```

3. **Translate the strings** in Qt Linguist or any text editor.

4. **Drop the `.ts` file** into `translations/`. CMake picks it up automatically on the next `cmake ..` run.

5. **Optionally add `qtbase_es.qm`** from your Qt installation into `translations/` so standard dialog buttons translate too.

6. **Rebuild** — `lrelease` compiles `.ts` → `.qm` and it is embedded.

If Qt ships a locale-qualified name for the new language (e.g. `pt_BR` instead of `pt`), add one entry to the mapping in `CMakeLists.txt` and one entry in the `qtBaseLang` lambda in `src/main.cpp`.

---

## 16. Adding a New Currency

Open `src/utils/CurrencyManager.cpp` and append a row to `s_currencies`:

```cpp
{ "NGN", "₦", "Nigerian Naira", 2, false, false },
//  code  symbol  englishName  decimals  symbolAfter  spaceBeforeSymbol
```

Recompile. No other changes needed — the currency picker and all formatting code reads from this table.

---

## 17. Schema Migrations

The schema version is stored in SQLite's built-in `PRAGMA user_version`. `Migrations::run()` is called once per launch after `createTables()`.

Rules:
- Fresh installs write `CURRENT_VERSION` directly in `createTables()` and never run any migrations.
- Post-ship changes get their own `static void migrate_N(QSqlDatabase& db)` function in `Migrations.cpp`.
- `Migrations::run()` checks `version < N` and applies each pending migration in order.
- `CURRENT_VERSION` in `Migrations.h` is incremented each time a migration is added.
- Never modify an existing `migrate_N()` after it has shipped.

Example — adding a new column after v1.0 ships:

```cpp
// In Migrations.cpp:
static void migrate_2(QSqlDatabase& db) {
    QSqlQuery q(db);
    q.exec("ALTER TABLE employees ADD COLUMN department TEXT DEFAULT ''");
}

void Migrations::run(QSqlDatabase& db) {
    int version = ...; // read PRAGMA user_version
    if (version < 2) migrate_2(db);
    // write new version
}

// In Migrations.h:
constexpr int CURRENT_VERSION = 2;

// In DatabaseManager::createTables():
// Add the column to the base schema so fresh installs include it.
```

---

## 18. Update Checker

`UpdateChecker` is a thin, self-contained `QObject` that queries the GitHub Releases API and compares the response against the running version. It is the only component in the codebase that makes outbound network requests.

### Two modes

- **Silent** (`check(true)`) — used by the automatic background check. Only emits `updateAvailable`. Network failures and "already up to date" are silently swallowed. The object deletes itself after emitting.
- **Manual** (`check(false)`) — used by Help → Check for Updates. Emits all three signals (`updateAvailable`, `alreadyUpToDate`, `checkFailed`) so the UI can always give feedback.

### No interruption design

The silent check fires 3 seconds after launch via `QTimer::singleShot`. On finding a new version it calls `MainWindow::onUpdateFound()` which:

1. Badges the Help menu title (`&Help  ●`) — visible without opening the menu
2. Changes the action text to `"Update Available — vX.Y.Z  🆕"` — visible the moment the user opens Help

No popup is shown. The user finishes what they were doing and notices the badge naturally.

### Dismissed version guard

`SettingsManager` stores `lastDismissedUpdate` (the version string the user last acknowledged). The silent check skips badging if the found version matches this value, preventing the badge from reappearing every launch after the user has already seen it.

### Privacy

The only data transmitted is the app version string in the HTTP `User-Agent` header (`Rawatib/1.0.0`). No personal data, no database content, no machine identifiers.

### Network dependency

`Qt6::Network` is the only addition to the build. The update checker is entirely optional from the user's perspective — network failure produces no visible effect in silent mode.

---

## 19. Known Constraints & Design Decisions

**Single-instance enforcement** — a `.lock` file next to the database prevents two processes from opening the same DB simultaneously. This is critical because SQLCipher does not handle concurrent writers.

**QMessageBox debugging** — the app runs with `WIN32_EXECUTABLE TRUE` (no console window on Windows). `qDebug()` output is invisible at runtime. During development, `QMessageBox` is used to surface debug information instead of `qDebug()`.

**Fallback key is public** — the compiled-in fallback key is visible in the open-source code. This is a documented and accepted trade-off. It protects against casual file browsing; it does not protect against an attacker with source access and full machine control. The admin PIN is required for real protection.

**Translation context per file** — each `.cpp` file that calls `tr()` defines its own static inline wrapper binding to a translation context that matches its class or namespace name. This keeps translation strings scoped and avoids collisions between files that have the same string in different languages.

**CSV export always in English** — CSV files use English column headers and fixed strings regardless of the active UI language. This ensures that a file exported in Arabic UI can be re-imported on a French UI without errors. XLSX and print reports use localised month names.

**Deduction fields stored at write time** — `baseDailyRate`, `dayDeduction`, and `appliedDeductionMode` are stored in each attendance record at the moment it is written, not recalculated on read. This means salary history is accurate even after the employee's salary or the deduction policy changes later.

**Two-pass import** — the import preview dialog exists specifically to give the admin full visibility and control before any data is written. Skipping to a single-pass import would be simpler but would remove the ability to review conflicts and wage mismatches before committing. The design prioritises correctness and transparency over convenience.

**No QThread / no async** — all operations run on the main Qt event loop thread. The database is fast enough for the dataset sizes a small business generates (hundreds to low thousands of records) that background threading adds complexity without meaningful benefit.
