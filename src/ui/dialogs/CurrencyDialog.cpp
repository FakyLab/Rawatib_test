#include "ui/dialogs/CurrencyDialog.h"
#include "utils/CurrencyManager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCompleter>

CurrencyDialog::CurrencyDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Currency"));
    setMinimumWidth(320);
    setMaximumWidth(460);
    setSizeGripEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 12);

    auto* promptLabel = new QLabel(
        tr("Choose the currency used for wages and reports."), this);
    promptLabel->setWordWrap(true);
    layout->addWidget(promptLabel);

    // ── Combo box ─────────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_combo = new QComboBox(this);
    m_combo->setEditable(true);
    m_combo->setInsertPolicy(QComboBox::NoInsert);
    m_combo->setLayoutDirection(Qt::LeftToRight);   // always LTR for currency names

    // Populate — display: "Egyptian Pound — ج.م (EGP)"
    // Store ISO code as item data for retrieval
    const QString current = CurrencyManager::code();
    int currentIndex = 0;
    const auto& currencies = CurrencyManager::allCurrencies();
    for (int i = 0; i < currencies.size(); ++i) {
        const auto& c = currencies[i];
        const QString label = QString("%1  —  %2  (%3)")
                                  .arg(c.englishName)
                                  .arg(c.symbol)
                                  .arg(c.code);
        m_combo->addItem(label, c.code);
        if (c.code == current)
            currentIndex = i;
    }
    m_combo->setCurrentIndex(currentIndex);

    // Completer for type-to-filter — searches the display text
    auto* completer = new QCompleter(m_combo->model(), m_combo);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_combo->setCompleter(completer);

    form->addRow(tr("Currency:"), m_combo);
    layout->addLayout(form);

    // ── Preview ───────────────────────────────────────────────────────────
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(
        "QLabel { background: palette(base); border: 1px solid palette(mid);"
        " border-radius: 4px; padding: 8px; font-size: 14pt; }"
    );
    layout->addWidget(m_preview);

    auto* previewHint = new QLabel(tr("Sample: 1,234.50"), this);
    previewHint->setAlignment(Qt::AlignCenter);
    previewHint->setStyleSheet("color: gray; font-size: 9pt;");
    layout->addWidget(previewHint);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CurrencyDialog::onCurrencyChanged);

    onCurrencyChanged(currentIndex);   // populate preview immediately
}

void CurrencyDialog::onCurrencyChanged(int index) {
    if (index < 0 || !m_preview) return;
    const QString code = m_combo->itemData(index).toString();
    const CurrencyInfo info = CurrencyManager::findByCode(code);

    // Preview: format 1234.50 using selected currency's rules
    // temporarily override to show the selected currency, not the stored one
    const double sample = 1234.50;
    const QString number = QLocale(QLocale::C).toString(sample, 'f', info.decimals);
    QString formatted;
    if (info.symbolAfter)
        formatted = info.spaceBeforeSymbol ? number + " " + info.symbol
                                           : number + info.symbol;
    else
        formatted = info.spaceBeforeSymbol ? info.symbol + " " + number
                                           : info.symbol + number;

    m_preview->setText(formatted);
}

// static
bool CurrencyDialog::show(QWidget* parent) {
    const QString before = CurrencyManager::code();
    CurrencyDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return false;

    const int idx = dlg.m_combo->currentIndex();
    if (idx < 0) return false;
    const QString chosen = dlg.m_combo->itemData(idx).toString();
    if (chosen == before) return false;

    CurrencyManager::setCurrent(chosen);
    return true;
}
