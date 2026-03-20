#include "utils/DeductionPolicy.h"
#include "database/DatabaseManager.h"
#include <QCoreApplication>

// ── Storage keys ──────────────────────────────────────────────────────────
static constexpr const char* KEY_MODE        = "deduction_mode";
static constexpr const char* KEY_PENALTY_PCT = "perday_penalty_pct";

// ── Defaults ──────────────────────────────────────────────────────────────
static constexpr const char* DEFAULT_MODE        = "perminute";
static constexpr double      DEFAULT_PENALTY_PCT = 50.0;

namespace DeductionPolicy {

// ── String conversion ─────────────────────────────────────────────────────

QString modeToString(Mode m) {
    switch (m) {
        case Mode::PerMinute: return QStringLiteral("perminute");
        case Mode::PerDay:    return QStringLiteral("perday");
        case Mode::Off:       return QStringLiteral("off");
    }
    return QStringLiteral("perminute");
}

Mode modeFromString(const QString& s) {
    if (s == QStringLiteral("perday")) return Mode::PerDay;
    if (s == QStringLiteral("off"))    return Mode::Off;
    return Mode::PerMinute;  // default for unknown/missing
}

QString modeLabel(Mode m) {
    switch (m) {
        case Mode::PerMinute:
            return QCoreApplication::translate("DeductionPolicy", "Per-minute");
        case Mode::PerDay:
            return QCoreApplication::translate("DeductionPolicy", "Per-day penalty");
        case Mode::Off:
            return QCoreApplication::translate("DeductionPolicy", "Off");
    }
    return QCoreApplication::translate("DeductionPolicy", "Per-minute");
}

// ── Readers ───────────────────────────────────────────────────────────────

Mode mode() {
    auto& db = DatabaseManager::instance();
    if (!db.isOpen()) return Mode::PerMinute;  // safe default before DB open
    return modeFromString(db.getDbSetting(KEY_MODE, DEFAULT_MODE));
}

double perDayPenaltyPct() {
    auto& db = DatabaseManager::instance();
    if (!db.isOpen()) return DEFAULT_PENALTY_PCT;
    bool ok = false;
    const double v = db.getDbSetting(
        KEY_PENALTY_PCT,
        QString::number(DEFAULT_PENALTY_PCT)).toDouble(&ok);
    if (!ok || v < 0.0 || v > 100.0) return DEFAULT_PENALTY_PCT;
    return v;
}

// ── Writers ───────────────────────────────────────────────────────────────

void setMode(Mode m) {
    DatabaseManager::instance().setDbSetting(KEY_MODE, modeToString(m));
}

void setPenaltyPct(double pct) {
    // Clamp to valid range before storing
    const double clamped = qBound(0.0, pct, 100.0);
    DatabaseManager::instance().setDbSetting(
        KEY_PENALTY_PCT,
        QString::number(clamped, 'f', 1));
}

} // namespace DeductionPolicy
