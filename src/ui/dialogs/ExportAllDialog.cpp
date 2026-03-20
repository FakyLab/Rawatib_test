#include "ui/dialogs/ExportAllDialog.h"
#include "repositories/AttendanceRepository.h"
#include "utils/SettingsManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QFrame>
#include <QDate>
#include <QLocale>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QPushButton>

// Returns the QLocale matching the active UI language — for month names.
static inline QLocale dlgLocale() {
    const QString lang = SettingsManager::getLanguage();
    if (lang == "ar") return QLocale(QLocale::Arabic);
    if (lang == "fr") return QLocale(QLocale::French);
    if (lang == "tr") return QLocale(QLocale::Turkish);
    if (lang == "zh") return QLocale(QLocale::Chinese);
    if (lang == "ja") return QLocale(QLocale::Japanese);
    if (lang == "ko") return QLocale(QLocale::Korean);
    return QLocale::c();
}

// ── Constructor ────────────────────────────────────────────────────────────

ExportAllDialog::ExportAllDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export All Employees"));
    setMinimumWidth(360);
    setMaximumWidth(500);
    setSizeGripEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 12);

    // ── Description ───────────────────────────────────────────────────────
    auto* descLabel = new QLabel(
        tr("Export attendance records for all employees."), this);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // ── Separator ─────────────────────────────────────────────────────────
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep1);

    // ── Period ────────────────────────────────────────────────────────────
    auto* periodLabel = new QLabel(tr("Period"), this);
    periodLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(periodLabel);

    // Month + year pickers on one row
    auto* pickerRow = new QHBoxLayout();
    pickerRow->setSpacing(8);

    m_monthCombo = new QComboBox(this);
    const QLocale loc = dlgLocale();
    const int currentMonth = QDate::currentDate().month();
    for (int m = 1; m <= 12; ++m)
        m_monthCombo->addItem(loc.monthName(m, QLocale::LongFormat), m);
    m_monthCombo->setCurrentIndex(currentMonth - 1);

    m_yearCombo = new QComboBox(this);
    const int currentYear = QDate::currentDate().year();
    // Range: earliest record year to current year
    const int earliestYear = AttendanceRepository::instance().getEarliestRecordYear();
    for (int y = currentYear; y >= qMin(earliestYear, currentYear - 5); --y)
        m_yearCombo->addItem(QString::number(y), y);
    m_yearCombo->setCurrentIndex(0);

    pickerRow->addWidget(m_monthCombo, 1);
    pickerRow->addWidget(m_yearCombo, 0);
    layout->addLayout(pickerRow);

    // All Time checkbox
    m_allTimeCheck = new QCheckBox(tr("All Time"), this);
    m_allTimeCheck->setChecked(false);
    layout->addWidget(m_allTimeCheck);

    connect(m_allTimeCheck, &QCheckBox::toggled,
            this, &ExportAllDialog::onAllTimeToggled);

    // ── Separator ─────────────────────────────────────────────────────────
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep2);

    // ── Format ────────────────────────────────────────────────────────────
    auto* formatLabel = new QLabel(tr("Format"), this);
    formatLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(formatLabel);

    m_csvRadio = new QRadioButton(
        tr("CSV  —  use this to re-import records later"), this);
    m_csvRadio->setChecked(true);

// XLSX option — disabled with a note when QXlsx is not available
#if __has_include(<xlsxdocument.h>)
    m_xlsxRadio = new QRadioButton(
        tr("XLSX  —  use this to open in Excel"), this);
#else
    m_xlsxRadio = new QRadioButton(
        tr("XLSX  —  not available (QXlsx not installed)"), this);
    m_xlsxRadio->setEnabled(false);
#endif

    layout->addWidget(m_csvRadio);
    layout->addWidget(m_xlsxRadio);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(this);
    auto* exportBtn = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    exportBtn->setDefault(true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ── Slots ──────────────────────────────────────────────────────────────────

void ExportAllDialog::onAllTimeToggled(bool checked) {
    m_monthCombo->setEnabled(!checked);
    m_yearCombo->setEnabled(!checked);
}

// ── Accessors ──────────────────────────────────────────────────────────────

bool ExportAllDialog::isAllTime() const {
    return m_allTimeCheck->isChecked();
}

int ExportAllDialog::selectedMonth() const {
    if (isAllTime()) return 0;
    return m_monthCombo->currentData().toInt();
}

int ExportAllDialog::selectedYear() const {
    if (isAllTime()) return 0;
    return m_yearCombo->currentData().toInt();
}

bool ExportAllDialog::isCsv() const {
    return m_csvRadio->isChecked();
}
