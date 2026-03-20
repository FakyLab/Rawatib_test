#pragma once
#include "models/AttendanceRecord.h"
#include "models/Employee.h"
#include <QString>
#include <QStringList>
#include <QDate>
#include <QTime>
#include <QVector>
#include <QWidget>
#include <optional>

// ── ImportHelper ───────────────────────────────────────────────────────────
//
// Imports attendance records from a CSV file exported by Rawatib.
//
// The CSV format is exactly what ExportHelper::exportMonth() produces:
//   - Header block (Employee, Period, [Hourly Wage|Monthly Salary], Exported)
//   - Column header row (Date, Check-In, Check-Out, Hours, Daily Wage, Status)
//   - Data rows
//   - Blank line
//   - Summary block (used as checksum)
//   - Optional Payroll Adjustments block (ignored)
//
// Multiple employee blocks in the same file are supported.
//
// Design: two-pass import.
//   Pass 1 — parseFile()   : parse + validate entire file, no DB writes.
//   Pass 2 — commitImport(): execute admin-approved result in one transaction.
//
// Between passes, ImportPreviewDialog shows the admin a row-level preview
// with checkboxes whenever conflicts or issues are detected. On a clean
// file the preview is skipped and a simple confirm dialog is shown instead.

namespace ImportHelper {

// ── ParsedRecord ───────────────────────────────────────────────────────────
//
// One parsed data row from the CSV, with its validation status and
// admin selection state.

struct ParsedRecord {
    int     csvRowNumber = 0;

    // Data read directly from CSV — the ground truth
    QDate   date;
    QTime   checkIn;
    QTime   checkOut;           // invalid = open record
    double  hoursWorked   = 0.0;
    double  dailyWage     = 0.0;   // col 4 — Net Day for monthly, Daily Wage for hourly
    bool    paid          = false;
    bool    isOpen        = false;
    // Monthly-only transparency fields — cols 6 & 7 in CSV (0.0 for hourly/absent)
    double  baseDailyRate = 0.0;
    double  dayDeduction  = 0.0;

    // Validation result
    enum class Status {
        Clean,          // no issues — selected by default
        SoftConflict,   // overlap with existing DB record — selected but flagged
        HardError       // bad data — deselected, shown in red
    };
    Status  status = Status::Clean;
    QString issueDescription;   // shown as tooltip / warning text in preview

    // Admin selection state (set in ImportPreviewDialog)
    bool    selected = true;
};

// ── ParsedEmployee ─────────────────────────────────────────────────────────
//
// One employee block parsed from the CSV, including DB resolution state
// and admin decisions (set in ImportPreviewDialog).

struct ParsedEmployee {
    // From CSV header
    QString csvName;
    double  csvWage        = 0.0;
    bool    wageParseOk    = false;
    PayType csvPayType     = PayType::Hourly;
    int     csvWorkingDays = 26;
    QTime   csvExpectedCheckin;    // invalid = not set / flexible schedule
    QTime   csvExpectedCheckout;   // invalid = not set / flexible schedule
    QString period;
    int     year           = 0;
    int     month          = 0;
    QString exportedDate;

    // CSV summary block values — used as checksum after parsing
    double  csvSummaryTotalHours  = -1.0;   // -1 = not found in file
    double  csvSummaryTotalSalary = -1.0;
    bool    checksumMismatch      = false;
    QString checksumNote;

    // DB resolution — determined during pass 1
    enum class Resolution {
        UseExisting,        // found in DB, wages match
        UseExistingWarn,    // found in DB, wage mismatch
        CreateNew,          // not found — will be auto-created
        Skip                // admin chose to skip entire employee
    };
    Resolution              resolution = Resolution::UseExisting;
    std::optional<Employee> existingEmployee;   // set if found in DB
    QString                 suggestedNewName;   // "Ahmed Ali (2)" etc.

    // Admin wage decision (only relevant for UseExistingWarn)
    enum class WageDecision {
        RecalculateCurrent, // recalculate hoursWorked/dailyWage at current DB wage
        KeepCsvValues,      // use hoursWorked/dailyWage exactly as in CSV
        CreateNew           // treat as new employee instead of updating existing
    };
    WageDecision wageDecision = WageDecision::RecalculateCurrent;

    // Manual wage entered by admin in ImportPreviewDialog when
    // resolution == CreateNew and wageParseOk == false (wage-omitted CSV).
    // 0.0 means not entered — employee created with wage 0 (can edit later).
    double manualWage = 0.0;

    QVector<ParsedRecord> records;

    // Convenience counts
    int cleanCount()        const;
    int softConflictCount() const;
    int hardErrorCount()    const;
    int selectedCount()     const;
    int totalCount()        const { return records.size(); }
};

// ── ParsePass1Result ───────────────────────────────────────────────────────
//
// Full output of pass 1. Passed to ImportPreviewDialog, then to commitImport().

struct ParsePass1Result {
    QVector<ParsedEmployee> employees;

    bool    hasAnyIssues    = false;  // any soft or hard issues anywhere
    bool    hasAnyImportable = false; // at least one selectable record exists

    QString fileError;  // non-empty if file could not be opened/read

    // Total selected record count across all employees
    int totalSelectedCount() const;
};

// ── ImportResult ───────────────────────────────────────────────────────────
//
// Final result returned after pass 2 — shown in result summary dialog.

struct ImportResult {
    int         imported = 0;   // records successfully inserted
    int         skipped  = 0;   // skipped: overlap caught by DB guard
    int         failed   = 0;   // failed: DB error on insert
    int         created  = 0;   // employees auto-created
    QStringList warnings;       // overlap/failure details
    QStringList notices;        // wage mismatch notices, auto-create notices
};

// ── Public API ─────────────────────────────────────────────────────────────

// Full import flow: file picker → pass 1 → optional preview dialog → pass 2.
// Returns final ImportResult. Returns default (all zeros) if user cancelled.
ImportResult importAttendance(QWidget* parent);

// Exposed for ImportPreviewDialog to call pass 2 directly after admin approval.
ImportResult commitImport(ParsePass1Result& pass1);

} // namespace ImportHelper
