#pragma once
#include <QString>
#include <QWidget>
#include "models/Employee.h"
#include "utils/PayrollCalculator.h"

// ── ExportHelper ──────────────────────────────────────────────────────────
//
// Provides export-to-file functionality for attendance data.
//
//   exportMonth()  — single employee, one month.
//                    Called from SalaryTab's "Export..." button.
//                    Shows a save dialog; user chooses .xlsx or .csv.
//
//   exportAll()    — all employees, one month or all time.
//                    Called from File → Export All Employees... (Ctrl+Shift+E).
//                    Shows ExportAllDialog (period + format), then a save dialog.
//                    Generated CSV is fully compatible with ImportHelper.
//
// payrollResult: pass a valid Result when payroll rules are enabled so
// the net pay breakdown is included in the export. Pass a default-constructed
// Result (grossPay == 0) to omit it.
//
// wagesVisible: when false (HideWages active, no unlock), wage figures are
// omitted from the output — consistent with what is shown on screen.
// Defaults to true so callers that don't care about this pass nothing.
//
// Safe to call from the UI thread — handles its own dialogs and file I/O.

namespace ExportHelper {

// Single employee, one month — called from SalaryTab.
void exportMonth(const QString& employeeName,
                 int employeeId,
                 int month, int year,
                 const Employee& employee,
                 const PayrollCalculator::Result& payrollResult,
                 QWidget* parent,
                 bool wagesVisible = true);

// All employees, one month or all time — called from File menu.
// Shows ExportAllDialog, then a save-file dialog.
// month == 0 and year == 0 means All Time (export every record per employee).
void exportAll(QWidget* parent,
               bool wagesVisible = true);

} // namespace ExportHelper
