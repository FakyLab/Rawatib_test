#pragma once
#include <QString>

// ── DeductionPolicy ───────────────────────────────────────────────────────
// Company-wide setting that controls how late arrival and early departure
// are penalised for monthly salary employees.
//
// Stored in app_settings (encrypted DB) under two keys:
//   "deduction_mode"      — "perminute" | "perday" | "off"
//   "perday_penalty_pct"  — double 0–100, used only in PerDay mode
//
// This is a monthly-only concept. Hourly employees are unaffected —
// their wage is always hours × rate regardless of this setting.
//
// Mode semantics:
//   PerMinute — deduct (lateMin + earlyMin) × perMinuteRate from dailyWage.
//               Proportional and continuous. Default.
//   PerDay    — if lateMinutes > 0 (after tolerance): deduct penaltyPct% of
//               dailyRate. Same independently for earlyMinutes > 0.
//               Uses existing lateToleranceMin per employee as threshold.
//               Maximum deduction per day = 2 × penaltyPct% (late AND early),
//               clamped to dailyRate by the existing qMax(0, ...) guard.
//   Off       — no automatic deduction at all. lateMinutes and earlyMinutes
//               are still recorded for display, but dayDeduction = 0 and
//               dailyWage = dailyBaseRate always. Admin handles penalties
//               manually via payroll rules.

namespace DeductionPolicy {

enum class Mode { PerMinute, PerDay, Off };

// ── Readers ───────────────────────────────────────────────────────────────
// Safe to call before DatabaseManager::initialize() only for Mode::PerMinute
// (the default). After DB is open, reads from app_settings each call.
// Lightweight — app_settings uses a prepared SELECT.

Mode   mode();
double perDayPenaltyPct();

// ── Writers ───────────────────────────────────────────────────────────────
// Require DatabaseManager::initialize() to have succeeded.

void setMode(Mode m);
void setPenaltyPct(double pct);

// ── Helpers ───────────────────────────────────────────────────────────────

// Convert Mode ↔ string for storage
QString   modeToString(Mode m);
Mode      modeFromString(const QString& s);

// Human-readable label for display
QString   modeLabel(Mode m);

} // namespace DeductionPolicy
