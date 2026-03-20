#pragma once
#include <QString>
#include <QVariant>

namespace SettingsManager {

// ── Language ──────────────────────────────────────────────────────────────
QString getLanguage();
void    setLanguage(const QString& langCode);

// ── First-run flag ────────────────────────────────────────────────────────
// True on a fresh install — used to trigger the first-run setup dialog.
// Set to false after the user completes first-run setup.
bool isFirstRun();
void setFirstRunComplete();

// ── Admin session timeout ─────────────────────────────────────────────────
// Duration in minutes after which admin is auto-locked on inactivity.
// 0 means disabled. Default: 5 minutes.
int  getSessionTimeout();
void setSessionTimeout(int minutes);

// ── Generic key-value access ──────────────────────────────────────────────
// For storing miscellaneous non-sensitive preferences (e.g. dismissed update
// version). Follows the same QSettings instance as all other settings.
QVariant value(const QString& key, const QVariant& defaultValue = QVariant());
void     setValue(const QString& key, const QVariant& value);

void resetAll();

} // namespace SettingsManager

