#pragma once
#include <QString>
#include <QVector>

struct CurrencyInfo {
    QString code;               // ISO 4217 — "EGP", "USD", "SAR" …
    QString symbol;             // Display symbol — "ج.م", "$", "ر.س" …
    QString englishName;        // Universal display name — "Egyptian Pound", "Euro" …
    int     decimals;           // Decimal places: 2 for most, 3 for KWD/BHD/JOD, 0 for some
    bool    symbolAfter;        // true → "1,234.50 ج.م"   false → "$1,234.50"
    bool    spaceBeforeSymbol;  // true → "1,234 ج.م"      false → "$1,234"
};

namespace CurrencyManager {

// ── Currency table ────────────────────────────────────────────────────────
const QVector<CurrencyInfo>& allCurrencies();
CurrencyInfo findByCode(const QString& code);  // returns EGP if not found

// ── Active currency (persisted in QSettings) ─────────────────────────────
CurrencyInfo current();
void         setCurrent(const QString& isoCode);

// Returns true if the user has explicitly chosen a currency (key exists in
// settings). False on a fresh install — used to trigger the first-run prompt.
bool isSet();

// ── Formatting ────────────────────────────────────────────────────────────
// Format a raw double using the active currency's symbol, position,
// decimal places and locale-aware thousands separator.
// e.g.  format(1234.5)  →  "1,234.50 ج.م"  or  "$1,234.50"
QString format(double amount);

// Wrap a formatted amount in <span dir="ltr"> for correct rendering
// inside RTL HTML — call this in PrintHelper instead of format() directly.
QString formatHtml(double amount);

// Symbol only — for column headers: "Wage (ج.م)" etc.
QString symbol();

// Code only — "EGP", "USD" etc.
QString code();

} // namespace CurrencyManager
