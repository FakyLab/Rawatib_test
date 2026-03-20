#include "ui/dialogs/FirstRunDialog.h"
#include "utils/CurrencyManager.h"
#include "utils/SettingsManager.h"
#include "utils/LanguageRegistry.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCompleter>
#include <QPushButton>
#include <QLocale>

// ── Constructor ───────────────────────────────────────────────────────────

FirstRunDialog::FirstRunDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Welcome to Rawatib"));
    setMinimumWidth(420);
    setMaximumWidth(560);
    setSizeGripEnabled(false);
    // No close button — user must make a choice
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 16);

    // ── Welcome message ───────────────────────────────────────────────────
    auto* titleLabel = new QLabel(tr("Welcome to Rawatib"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto* subLabel = new QLabel(
        tr("Before you start, choose your language and currency.\n"
           "You can change these later in the View menu."),
        this);
    subLabel->setWordWrap(true);
    subLabel->setAlignment(Qt::AlignCenter);
    QPalette pal = subLabel->palette();
    pal.setColor(QPalette::WindowText, subLabel->palette().color(QPalette::Mid));
    subLabel->setPalette(pal);
    layout->addWidget(subLabel);

    // ── Form ──────────────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Language picker
    m_langCombo = new QComboBox(this);
    m_langCombo->setLayoutDirection(Qt::LeftToRight);

    // Detect system locale to pre-select the best matching language.
    // QLocale::system().name() returns e.g. "ar_EG", "fr_FR", "tr_TR".
    // We match the two-letter prefix against registered language codes.
    const QString systemPrefix = QLocale::system().name().left(2).toLower();
    int langDefault = 0;
    const QVector<LanguageInfo>& langs = LanguageRegistry::all();
    for (int i = 0; i < langs.size(); ++i) {
        m_langCombo->addItem(langs[i].nativeName, langs[i].code);
        if (langs[i].code == systemPrefix)
            langDefault = i;
    }
    m_langCombo->setCurrentIndex(langDefault);
    form->addRow(tr("Language:"), m_langCombo);

    // Currency picker
    m_combo = new QComboBox(this);
    m_combo->setEditable(true);
    m_combo->setInsertPolicy(QComboBox::NoInsert);
    m_combo->setLayoutDirection(Qt::LeftToRight);

    const auto& currencies = CurrencyManager::allCurrencies();
    int defaultIndex = 0;
    for (int i = 0; i < currencies.size(); ++i) {
        const auto& c = currencies[i];
        const QString label = QString("%1  —  %2  (%3)")
                                  .arg(c.englishName)
                                  .arg(c.symbol)
                                  .arg(c.code);
        m_combo->addItem(label, c.code);
        if (c.code == "EGP")
            defaultIndex = i;
    }
    m_combo->setCurrentIndex(defaultIndex);

    auto* completer = new QCompleter(m_combo->model(), m_combo);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_combo->setCompleter(completer);

    form->addRow(tr("Currency:"), m_combo);
    layout->addLayout(form);

    // ── Live currency preview ─────────────────────────────────────────────
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(
        "QLabel { background: palette(base); border: 1px solid palette(mid);"
        " border-radius: 4px; padding: 10px; font-size: 16pt; font-weight: bold; }");
    layout->addWidget(m_preview);

    auto* hint = new QLabel(tr("Sample: 1,234.50"), this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color: gray; font-size: 9pt;");
    layout->addWidget(hint);

    // ── Discovery hint ────────────────────────────────────────────────────
    // Shown once at first run — points users to Help → Discover Rawatib
    // without being aggressive about it.
    auto* discoverHint = new QLabel(
        tr("💡 Tip: Explore <b>Help → Discover Rawatib</b> to learn hidden features at your own pace."),
        this);
    discoverHint->setWordWrap(true);
    discoverHint->setAlignment(Qt::AlignCenter);
    discoverHint->setStyleSheet(
        "QLabel { color: palette(mid); font-size: 9pt; "
        "background: palette(base); border: 1px solid palette(midlight); "
        "border-radius: 4px; padding: 8px 10px; }");
    layout->addWidget(discoverHint);

    // ── Button ────────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(this);
    auto* startBtn = buttons->addButton(tr("Get Started"), QDialogButtonBox::AcceptRole);
    startBtn->setDefault(true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FirstRunDialog::onLanguageChanged);
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FirstRunDialog::onCurrencyChanged);

    onCurrencyChanged(defaultIndex);
}

// ── Slots ─────────────────────────────────────────────────────────────────

void FirstRunDialog::onLanguageChanged(int index) {
    if (index < 0) return;
    // Just save the selection — translators are installed after this
    // dialog closes in main.cpp, so the whole MainWindow gets the right language
    Q_UNUSED(index)
}

void FirstRunDialog::onCurrencyChanged(int index) {
    if (index < 0 || !m_preview) return;
    const QString code = m_combo->itemData(index).toString();
    const CurrencyInfo info = CurrencyManager::findByCode(code);

    const double sample = 1234.50;
    const QString number = QLocale(QLocale::C).toString(sample, 'f', info.decimals);
    QString formatted;
    if (info.symbolAfter)
        formatted = info.spaceBeforeSymbol
            ? number + " " + info.symbol
            : number + info.symbol;
    else
        formatted = info.spaceBeforeSymbol
            ? info.symbol + " " + number
            : info.symbol + number;

    m_preview->setText(formatted);
}

// ── Static showIfNeeded ───────────────────────────────────────────────────

// static
void FirstRunDialog::showIfNeeded(QWidget* parent) {
    if (!SettingsManager::isFirstRun()) return;

    FirstRunDialog dlg(parent);
    dlg.exec();   // always accepted — no cancel button

    // Save language — main.cpp reads this immediately after and installs
    // the correct translators before constructing MainWindow
    const int langIdx = dlg.m_langCombo->currentIndex();
    if (langIdx >= 0) {
        const QString chosenCode = dlg.m_langCombo->itemData(langIdx).toString();
        SettingsManager::setLanguage(chosenCode);
    }

    // Save currency
    const int currIdx = dlg.m_combo->currentIndex();
    if (currIdx >= 0)
        CurrencyManager::setCurrent(dlg.m_combo->itemData(currIdx).toString());
    else
        CurrencyManager::setCurrent("EGP");

    // Mark first run complete — never show again
    SettingsManager::setFirstRunComplete();
}
