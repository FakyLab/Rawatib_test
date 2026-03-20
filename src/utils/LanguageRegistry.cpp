#include "utils/LanguageRegistry.h"

// ── Supported languages ────────────────────────────────────────────────────
//
// To add a language:
//   1. Add a row here — that's the only code change needed.
//   2. Drop rawatib_<code>.ts into translations/ and re-run CMake.
//      CMake's glob picks it up automatically — no CMakeLists.txt edit needed.
//   3. Optionally copy qtbase_<code>.qm from your Qt installation into
//      translations/ for translated Qt dialog buttons (OK, Cancel, etc.).
//
// isRtl: mark true only for genuine RTL scripts (Arabic, Persian, Urdu, Hebrew…).

static const QVector<LanguageInfo> s_languages = {
    { "en", "English",    false },
    { "ar", "العربية",    true  },
    { "fr", "Français",   false },
    { "tr", "Türkçe",     false },
    { "zh", "中文",        false },
    { "ko", "한국어",      false },
    { "ja", "日本語",      false },
    // Future languages — add a row here when the .ts file is ready:
    // { "es", "Español",   false },
    // { "de", "Deutsch",   false },
    // { "fa", "فارسی",     true  },
    // { "ur", "اردو",      true  },
    // { "pt", "Português", false },
    // { "id", "Indonesia", false },
};

namespace LanguageRegistry {

const QVector<LanguageInfo>& all() {
    return s_languages;
}

LanguageInfo find(const QString& code) {
    for (const auto& lang : s_languages)
        if (lang.code == code)
            return lang;
    return s_languages.first();   // fallback to English
}

bool isRtl(const QString& code) {
    return find(code).isRtl;
}

} // namespace LanguageRegistry
