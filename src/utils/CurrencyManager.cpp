#include "utils/CurrencyManager.h"
#include <QSettings>
#include <QLocale>

namespace CurrencyManager {

// ── Currency table ────────────────────────────────────────────────────────
// Columns: code, symbol, englishName, decimals, symbolAfter, spaceBeforeSymbol
//
// englishName is the universal display name shown in all currency pickers
// regardless of the active UI language — currency names in financial software
// are conventionally shown in English internationally.
//
// To add a currency: append a row and recompile. No other changes needed.

static const QVector<CurrencyInfo> s_currencies = {
    // ── Arab world ────────────────────────────────────────────────────────
    { "EGP", "ج.م",  "Egyptian Pound",       2, true,  true  },
    { "SAR", "ر.س",  "Saudi Riyal",           2, true,  true  },
    { "AED", "د.إ",  "UAE Dirham",            2, true,  true  },
    { "KWD", "د.ك",  "Kuwaiti Dinar",         3, true,  true  },
    { "BHD", "د.ب",  "Bahraini Dinar",        3, true,  true  },
    { "OMR", "ر.ع",  "Omani Rial",            3, true,  true  },
    { "QAR", "ر.ق",  "Qatari Riyal",          2, true,  true  },
    { "JOD", "د.ا",  "Jordanian Dinar",       3, true,  true  },
    { "IQD", "ع.د",  "Iraqi Dinar",           0, true,  true  },
    { "LYD", "ل.د",  "Libyan Dinar",          3, true,  true  },
    { "MAD", "د.م",  "Moroccan Dirham",       2, true,  true  },
    { "TND", "د.ت",  "Tunisian Dinar",        3, true,  true  },
    { "DZD", "د.ج",  "Algerian Dinar",        2, true,  true  },
    { "SDG", "ج.س",  "Sudanese Pound",        2, true,  true  },
    { "YER", "﷼",    "Yemeni Rial",           0, true,  true  },
    { "SYP", "ل.س",  "Syrian Pound",          2, true,  true  },
    { "LBP", "ل.ل",  "Lebanese Pound",        0, true,  true  },

    // ── Europe ────────────────────────────────────────────────────────────
    { "EUR", "€",    "Euro",                  2, false, false },
    { "GBP", "£",    "British Pound",         2, false, false },
    { "CHF", "CHF",  "Swiss Franc",           2, false, true  },
    { "TRY", "₺",    "Turkish Lira",          2, false, false },

    // ── Americas ──────────────────────────────────────────────────────────
    { "USD", "$",    "US Dollar",             2, false, false },

    // ── Africa ────────────────────────────────────────────────────────────
    { "NGN", "₦",    "Nigerian Naira",        2, false, false },
    { "KES", "KSh",  "Kenyan Shilling",       2, false, true  },
    { "ZAR", "R",    "South African Rand",    2, false, true  },
    { "XOF", "CFA",  "West African CFA Franc",0, false, true  },
    { "XAF", "FCFA", "Central African CFA Franc", 0, false, true },

    // ── Asia ──────────────────────────────────────────────────────────────
    { "PKR", "₨",    "Pakistani Rupee",       2, false, false },
    { "INR", "₹",    "Indian Rupee",          2, false, false },
    { "CNY", "¥",    "Chinese Yuan",          2, false, false },
    { "HKD", "HK$",  "Hong Kong Dollar",      2, false, false },
    { "TWD", "NT$",  "Taiwan Dollar",         0, false, false },
    { "JPY", "¥",    "Japanese Yen",          0, false, false },
    { "KRW", "₩",    "Korean Won",            0, false, false },
};

const QVector<CurrencyInfo>& allCurrencies() {
    return s_currencies;
}

CurrencyInfo findByCode(const QString& code) {
    for (const auto& c : s_currencies)
        if (c.code == code) return c;
    return s_currencies.first();   // default to EGP
}

// ── Settings ──────────────────────────────────────────────────────────────

static QSettings& settings() {
    static QSettings s("FakyLab", "Rawatib");
    return s;
}

CurrencyInfo current() {
    const QString code = settings().value("currency_code", "EGP").toString();
    return findByCode(code);
}

bool isSet() {
    return settings().contains("currency_code");
}

void setCurrent(const QString& isoCode) {
    settings().setValue("currency_code", isoCode);
    settings().sync();
}

// ── Formatting ────────────────────────────────────────────────────────────

QString format(double amount) {
    const CurrencyInfo cur = current();
    // QLocale::c() for LTR (e.g. "1,234.50"), Arabic locale for RTL numerals
    // We intentionally use QLocale::c() here so numbers are always Western
    // Arabic digits (0-9) in both UI directions — easier to read on receipts.
    const QString number = QLocale(QLocale::C).toString(amount, 'f', cur.decimals);

    if (cur.symbolAfter) {
        return cur.spaceBeforeSymbol
            ? number + " " + cur.symbol
            : number + cur.symbol;
    } else {
        return cur.spaceBeforeSymbol
            ? cur.symbol + " " + number
            : cur.symbol + number;
    }
}

QString formatHtml(double amount) {
    // Wrap in dir="ltr" so the number+symbol renders correctly
    // inside RTL HTML documents regardless of symbol position.
    return QString("<span dir=\"ltr\">%1</span>").arg(format(amount));
}

QString symbol() {
    return current().symbol;
}

QString code() {
    return current().code;
}

} // namespace CurrencyManager
