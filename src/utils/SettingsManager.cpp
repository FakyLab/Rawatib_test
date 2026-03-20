#include "utils/SettingsManager.h"
#include <QSettings>
#include <QLocale>

namespace SettingsManager {

static QSettings& settings() {
    static QSettings s("FakyLab", "Rawatib");
    return s;
}

// Detect system language on first run — default to Arabic only if the
// system locale is Arabic, otherwise fall back to English.
static QString systemDefaultLanguage() {
    const QString systemLang = QLocale::system().name(); // e.g. "ar_EG", "en_US"
    return systemLang.startsWith("ar") ? "ar" : "en";
}

QString getLanguage() {
    return settings().value("language", systemDefaultLanguage()).toString();
}

void setLanguage(const QString& langCode) {
    settings().setValue("language", langCode);
}

// ── First-run flag ────────────────────────────────────────────────────────

bool isFirstRun() {
    return !settings().contains("first_run_complete");
}

void setFirstRunComplete() {
    settings().setValue("first_run_complete", true);
    settings().sync();
}

int getSessionTimeout() {
    return settings().value("session_timeout_minutes", 5).toInt();
}

void setSessionTimeout(int minutes) {
    settings().setValue("session_timeout_minutes", minutes);
    settings().sync();
}

// ── Generic key-value access ──────────────────────────────────────────────

QVariant value(const QString& key, const QVariant& defaultValue) {
    return settings().value(key, defaultValue);
}

void setValue(const QString& key, const QVariant& val) {
    settings().setValue(key, val);
}

void resetAll() {
    settings().clear();
    settings().sync();
}

} // namespace SettingsManager

