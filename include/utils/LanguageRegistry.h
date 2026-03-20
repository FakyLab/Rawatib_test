#pragma once
#include <QString>
#include <QVector>

// ── LanguageRegistry ───────────────────────────────────────────────────────
//
// Single source of truth for every language the app supports.
// All language-driven logic — the first-run dialog, the View → Language
// menu, translator loading in main.cpp, and RTL detection — reads from
// this registry.  Adding a new language requires only:
//
//   1. Add an entry to s_languages in LanguageRegistry.cpp.
//   2. Drop translations/rawatib_<code>.ts (and its compiled .qm) into
//      the translations/ folder.  CMake picks it up automatically via
//      the glob in CMakeLists.txt.
//   3. Optionally copy qtbase_<code>.qm from the Qt installation into
//      translations/ so Qt's own dialog buttons translate too.
//
// No other code needs to change.
//
// ── Fields ────────────────────────────────────────────────────────────────
//   code        ISO 639-1 language code, e.g. "en", "ar", "fr", "tr"
//   nativeName  Name in the language's own script — shown in menus.
//               e.g. "English", "العربية", "Français", "Türkçe"
//   isRtl       true for right-to-left languages (Arabic, Farsi, Hebrew, Urdu)

struct LanguageInfo {
    QString code;
    QString nativeName;
    bool    isRtl = false;
};

namespace LanguageRegistry {

// All supported languages, in display order.
const QVector<LanguageInfo>& all();

// Look up a language by code. Returns English entry if not found.
LanguageInfo find(const QString& code);

// Returns true if the given language code is right-to-left.
bool isRtl(const QString& code);

} // namespace LanguageRegistry
