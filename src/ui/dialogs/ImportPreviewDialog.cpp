#include "ui/dialogs/ImportPreviewDialog.h"
#include "utils/CurrencyManager.h"
#include "utils/ThemeHelper.h"
#include "models/Employee.h"
#include <QHeaderView>
#include <QScrollArea>
#include <QSplitter>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QFrame>
#include <QApplication>
#include <QCoreApplication>
#include <QToolTip>
#include <QEvent>
#include <QDoubleSpinBox>

static inline QString tr(const char* key) {
    return QCoreApplication::translate("ImportPreviewDialog", key);
}

// ── Column indices ─────────────────────────────────────────────────────────
enum Col {
    COL_CHECK    = 0,
    COL_EMPLOYEE = 1,
    COL_DATE     = 2,
    COL_IN       = 3,
    COL_OUT      = 4,
    COL_HOURS    = 5,
    COL_WAGE     = 6,
    COL_STATUS   = 7,
    COL_ISSUE    = 8,
    COL_COUNT    = 9
};

// Row background colors — adapt to light/dark mode
static QColor rowColorClean() {
    return ThemeHelper::isDark() ? QColor("#1e1e1e") : QColor("#ffffff");
}
static QColor rowColorConflict() {
    return ThemeHelper::isDark() ? QColor("#2d2000") : QColor("#fff8e1");
}
static QColor rowColorError() {
    return ThemeHelper::isDark() ? QColor("#2d0000") : QColor("#ffebee");
}
static QColor rowColorConflictBorder() {
    return ThemeHelper::isDark() ? QColor("#8a6200") : QColor("#f9a825");
}
static QColor rowColorErrorBorder() {
    return ThemeHelper::isDark() ? QColor("#8b0000") : QColor("#c62828");
}

ImportPreviewDialog::ImportPreviewDialog(ImportHelper::ParsePass1Result& pass1,
                                          QWidget* parent)
    : QDialog(parent), m_pass1(pass1)
{
    setWindowTitle(tr("Import Preview"));
    setMinimumSize(700, 500);
    resize(960, 640);
    setSizeGripEnabled(true);
    setupUi();
}

void ImportPreviewDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // ── Summary header ─────────────────────────────────────────────────────
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px 0;");
    buildSummaryHeader();
    mainLayout->addWidget(m_summaryLabel);

    // ── Per-employee resolution area (scrollable) ─────────────────────────
    m_resolutionArea = new QWidget(this);
    auto* resLayout = new QVBoxLayout(m_resolutionArea);
    resLayout->setContentsMargins(0, 0, 0, 0);
    resLayout->setSpacing(6);
    buildEmployeeResolutionBoxes();
    mainLayout->addWidget(m_resolutionArea);

    // ── Separator ─────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep);

    // ── Filter + select-all controls ──────────────────────────────────────
    auto* controlRow = new QHBoxLayout();
    m_filterCheck = new QCheckBox(tr("Conflicts only"), this);
    m_filterCheck->blockSignals(true);
    m_filterCheck->setChecked(false);  // default: show all rows
    m_filterCheck->blockSignals(false);
    connect(m_filterCheck, &QCheckBox::toggled,
            this, &ImportPreviewDialog::onFilterToggled);

    m_selectAllCheck = new QCheckBox(tr("Select / Deselect all visible"), this);
    m_selectAllCheck->blockSignals(true);
    m_selectAllCheck->setChecked(true);
    m_selectAllCheck->blockSignals(false);
    connect(m_selectAllCheck, &QCheckBox::toggled,
            this, &ImportPreviewDialog::onSelectAllToggled);

    controlRow->addWidget(m_filterCheck);
    controlRow->addSpacing(24);
    controlRow->addWidget(m_selectAllCheck);
    controlRow->addStretch();
    mainLayout->addLayout(controlRow);

    // ── Preview table ──────────────────────────────────────────────────────
    m_table = new QTableWidget(0, COL_COUNT, this);
    m_table->setHorizontalHeaderLabels({
        "",
        tr("Employee"),
        tr("Date"),
        tr("Check-In"),
        tr("Check-Out"),
        tr("Hours"),
        tr("Daily Wage"),
        tr("Status"),
        tr("Issue")
    });
    m_table->horizontalHeader()->setSectionResizeMode(COL_CHECK,    QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_EMPLOYEE, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_DATE,     QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_IN,       QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_OUT,      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_HOURS,    QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_WAGE,     QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STATUS,   QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COL_ISSUE,    QHeaderView::Stretch);
    m_table->setColumnWidth(COL_CHECK, 28);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(true);

    connect(m_table, &QTableWidget::cellChanged,
            this, &ImportPreviewDialog::onRowCheckChanged);

    buildTable();
    mainLayout->addWidget(m_table, 1);

    // ── Bottom buttons ─────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_importBtn = new QPushButton(tr("Import Selected (0)"), this);
    m_importBtn->setDefault(true);
    m_importBtn->setStyleSheet(
        ThemeHelper::isDark()
        ? "QPushButton { background: #1976D2; color: white; "
          "padding: 6px 18px; border-radius: 4px; font-weight: bold; }"
          "QPushButton:disabled { background: #455A64; }"
        : "QPushButton { background: #1565C0; color: white; "
          "padding: 6px 18px; border-radius: 4px; font-weight: bold; }"
          "QPushButton:disabled { background: #90A4AE; }");

    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_importBtn, &QPushButton::clicked, this, &ImportPreviewDialog::onImportClicked);

    btnRow->addStretch();
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_importBtn);
    mainLayout->addLayout(btnRow);

    updateImportButton();
}

// ── Summary header text ────────────────────────────────────────────────────

void ImportPreviewDialog::buildSummaryHeader()
{
    int totalRecords = 0, totalConflicts = 0, totalErrors = 0, totalEmps = 0;
    for (const auto& emp : m_pass1.employees) {
        if (emp.totalCount() == 0) continue;
        ++totalEmps;
        totalRecords   += emp.totalCount();
        totalConflicts += emp.softConflictCount();
        totalErrors    += emp.hardErrorCount();
    }

    QString txt = tr("%1 employee(s) · %2 record(s)")
        .arg(totalEmps).arg(totalRecords);
    if (totalConflicts > 0)
        txt += tr(" · ⚠ %1 conflict(s)").arg(totalConflicts);
    if (totalErrors > 0)
        txt += tr(" · ✗ %1 error(s)").arg(totalErrors);

    m_summaryLabel->setText(txt);
}

// ── Per-employee resolution boxes ─────────────────────────────────────────

void ImportPreviewDialog::buildEmployeeResolutionBoxes()
{
    auto* layout = static_cast<QVBoxLayout*>(m_resolutionArea->layout());
    if (!layout) return;  // safety guard — should never happen

    for (int i = 0; i < m_pass1.employees.size(); ++i) {
        auto& emp = m_pass1.employees[i];

        // Only show a box for employees that need a decision
        const bool needsDecision =
            emp.resolution == ImportHelper::ParsedEmployee::Resolution::UseExistingWarn ||
            emp.resolution == ImportHelper::ParsedEmployee::Resolution::CreateNew ||
            emp.checksumMismatch;

        if (!needsDecision) continue;

        auto* box = new QGroupBox(emp.csvName, m_resolutionArea);
        box->setStyleSheet(
            ThemeHelper::isDark()
            ? "QGroupBox { font-weight: bold; border: 1px solid #F57C00; "
              "border-radius: 4px; margin-top: 6px; padding-top: 4px; }"
              "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
            : "QGroupBox { font-weight: bold; border: 1px solid #FFA726; "
              "border-radius: 4px; margin-top: 6px; padding-top: 4px; }"
              "QGroupBox::title { subcontrol-origin: margin; left: 8px; }");

        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setSpacing(4);

        // Wage mismatch resolution
        if (emp.resolution == ImportHelper::ParsedEmployee::Resolution::UseExistingWarn) {
            const bool isMonthly = (emp.csvPayType == PayType::Monthly);
            const double dbWage  = isMonthly
                ? emp.existingEmployee->monthlySalary
                : emp.existingEmployee->hourlyWage;

            const QString mismatchTxt = tr(
                "⚠  CSV wage: %1  ≠  Current wage: %2")
                .arg(CurrencyManager::format(emp.csvWage))
                .arg(CurrencyManager::format(dbWage));
            auto* mismatchLabel = new QLabel(mismatchTxt, box);
            mismatchLabel->setStyleSheet(ThemeHelper::isDark() ? "color: #ffb74d;" : "color: #E65100;");
            boxLayout->addWidget(mismatchLabel);

            auto* btnGroup = new QButtonGroup(box);

            // For monthly employees, recalculating from scratch isn't supported
            // (it would require expected times, tolerance, etc. that may have changed).
            // So we only offer Keep CSV values or Create as new.
            if (isMonthly) {
                auto* rbKeep   = new QRadioButton(
                    tr("Keep original values from CSV"), box);
                auto* rbCreate = new QRadioButton(
                    tr("Create as new employee \"%1\"").arg(emp.suggestedNewName), box);

                btnGroup->addButton(rbKeep,   1);
                btnGroup->addButton(rbCreate, 2);
                rbKeep->setChecked(true);   // default for monthly

                // Set the default decision immediately so commitImport sees it
                emp.wageDecision = ImportHelper::ParsedEmployee::WageDecision::KeepCsvValues;

                const int empIdx = i;
                connect(btnGroup, &QButtonGroup::idClicked, this,
                    [this, empIdx](int id){ onWageDecisionChanged(empIdx, id); });

                boxLayout->addWidget(rbKeep);
                boxLayout->addWidget(rbCreate);
            } else {
                auto* rbRecalc = new QRadioButton(
                    tr("Recalculate at current wage (%1)")
                        .arg(CurrencyManager::format(dbWage)), box);
                auto* rbKeep   = new QRadioButton(
                    tr("Keep original values from CSV"), box);
                auto* rbCreate = new QRadioButton(
                    tr("Create as new employee \"%1\"").arg(emp.suggestedNewName), box);

                btnGroup->addButton(rbRecalc, 0);
                btnGroup->addButton(rbKeep,   1);
                btnGroup->addButton(rbCreate, 2);
                rbRecalc->setChecked(true);   // default for hourly

                const int empIdx = i;
                connect(btnGroup, &QButtonGroup::idClicked, this,
                    [this, empIdx](int id){ onWageDecisionChanged(empIdx, id); });

                boxLayout->addWidget(rbRecalc);
                boxLayout->addWidget(rbKeep);
                boxLayout->addWidget(rbCreate);
            }
        }

        // Unknown employee resolution
        if (emp.resolution == ImportHelper::ParsedEmployee::Resolution::CreateNew) {
            const bool isMonthly = (emp.csvPayType == PayType::Monthly);

            if (emp.wageParseOk) {
                // Wage is known — show info and create/skip radio buttons
                const QString wageLabel = isMonthly
                    ? tr("Employee \"%1\" not found in database. "
                         "Will be created as monthly employee with salary %2.")
                          .arg(emp.csvName)
                          .arg(CurrencyManager::format(emp.csvWage))
                    : tr("Employee \"%1\" not found in database. "
                         "Will be created with wage %2.")
                          .arg(emp.csvName)
                          .arg(CurrencyManager::format(emp.csvWage));
                auto* infoLabel = new QLabel(wageLabel, box);
                infoLabel->setWordWrap(true);
                infoLabel->setStyleSheet("color: #1B5E20;");
                boxLayout->addWidget(infoLabel);

                auto* btnGroup = new QButtonGroup(box);
                auto* rbCreate = new QRadioButton(
                    tr("Create employee \"%1\"").arg(emp.suggestedNewName), box);
                auto* rbSkip   = new QRadioButton(tr("Skip this employee"), box);
                btnGroup->addButton(rbCreate, 0);
                btnGroup->addButton(rbSkip,   1);
                rbCreate->setChecked(true);

                const int empIdx = i;
                connect(btnGroup, &QButtonGroup::idClicked, this,
                    [this, empIdx](int id){ onEmployeeResolutionChanged(empIdx, id); });

                boxLayout->addWidget(rbCreate);
                boxLayout->addWidget(rbSkip);

            } else {
                // No wage in file (wage-omitted export) — let admin enter it
                auto* infoLabel = new QLabel(
                    tr("Employee \"%1\" not found and wage is not in the file.")
                          .arg(emp.csvName),
                    box);
                infoLabel->setWordWrap(true);
                infoLabel->setStyleSheet(ThemeHelper::isDark() ? "color: #ffb74d;" : "color: #E65100;");
                boxLayout->addWidget(infoLabel);

                auto* wageRow   = new QHBoxLayout();
                auto* wageLabel = new QLabel(
                    isMonthly ? tr("Monthly salary:") : tr("Hourly wage:"), box);
                auto* wageSpin  = new QDoubleSpinBox(box);
                wageSpin->setRange(0.0, 99999.99);
                wageSpin->setDecimals(2);
                wageSpin->setValue(0.0);
                wageSpin->setSuffix(isMonthly
                    ? " " + CurrencyManager::symbol() + tr("/mo")
                    : " " + CurrencyManager::symbol() + tr("/hr"));
                wageSpin->setMinimumWidth(120);
                auto* wageHint = new QLabel(
                    tr("(leave 0 to set later)"), box);
                wageHint->setStyleSheet("color: palette(mid); font-size: 9pt;");
                wageRow->addWidget(wageLabel);
                wageRow->addWidget(wageSpin);
                wageRow->addWidget(wageHint);
                wageRow->addStretch();
                boxLayout->addLayout(wageRow);

                auto* btnGroup = new QButtonGroup(box);
                auto* rbCreate = new QRadioButton(
                    tr("Create employee \"%1\"").arg(emp.suggestedNewName), box);
                auto* rbSkip   = new QRadioButton(tr("Skip this employee"), box);
                btnGroup->addButton(rbCreate, 0);
                btnGroup->addButton(rbSkip,   1);
                rbCreate->setChecked(true);

                const int empIdx = i;
                connect(btnGroup, &QButtonGroup::idClicked, this,
                    [this, empIdx](int id){ onEmployeeResolutionChanged(empIdx, id); });
                connect(wageSpin,
                    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this, empIdx](double v) {
                        if (empIdx < m_pass1.employees.size())
                            m_pass1.employees[empIdx].manualWage = v;
                    });

                boxLayout->addWidget(rbCreate);
                boxLayout->addWidget(rbSkip);
            }
        }

        // Checksum mismatch notice
        if (emp.checksumMismatch) {
            auto* checksumLabel = new QLabel(
                QString("⚠  %1").arg(emp.checksumNote), box);
            checksumLabel->setWordWrap(true);
            checksumLabel->setStyleSheet(ThemeHelper::isDark() ? "color: #ffb74d; font-style: italic;" : "color: #E65100; font-style: italic;");
            boxLayout->addWidget(checksumLabel);
        }

        layout->addWidget(box);
    }
}

// ── Table population ───────────────────────────────────────────────────────

void ImportPreviewDialog::buildTable()
{
    populateTable(m_filterCheck->isChecked());
}

void ImportPreviewDialog::populateTable(bool conflictsOnly)
{
    m_updatingChecks = true;
    m_table->setRowCount(0);
    m_rowMap.clear();

    for (int ei = 0; ei < m_pass1.employees.size(); ++ei) {
        const auto& emp = m_pass1.employees[ei];
        if (emp.resolution == ImportHelper::ParsedEmployee::Resolution::Skip) continue;

        for (int ri = 0; ri < emp.records.size(); ++ri) {
            const auto& rec = emp.records[ri];

            // Filter
            if (conflictsOnly && rec.status == ImportHelper::ParsedRecord::Status::Clean)
                continue;

            const int row = m_table->rowCount();
            m_table->insertRow(row);
            m_rowMap.append({ei, ri});

            // Background color — adapts to light/dark mode
            QColor bg = rowColorClean();
            if (rec.status == ImportHelper::ParsedRecord::Status::SoftConflict)
                bg = rowColorConflict();
            else if (rec.status == ImportHelper::ParsedRecord::Status::HardError)
                bg = rowColorError();

            auto setCell = [&](int col, const QString& text, bool enabled = true) {
                auto* item = new QTableWidgetItem(text);
                item->setBackground(bg);
                item->setFlags(enabled
                    ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                    : Qt::ItemIsSelectable);
                m_table->setItem(row, col, item);
                return item;
            };

            // Checkbox column
            auto* checkItem = new QTableWidgetItem();
            checkItem->setBackground(bg);
            if (rec.status == ImportHelper::ParsedRecord::Status::HardError) {
                checkItem->setFlags(Qt::ItemIsSelectable);  // disabled
                checkItem->setCheckState(Qt::Unchecked);
            } else {
                checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                checkItem->setCheckState(rec.selected ? Qt::Checked : Qt::Unchecked);
            }
            m_table->setItem(row, COL_CHECK, checkItem);

            setCell(COL_EMPLOYEE, emp.csvName);
            setCell(COL_DATE,     rec.date.toString("yyyy-MM-dd"));
            setCell(COL_IN,       rec.checkIn.toString("HH:mm"));
            setCell(COL_OUT,      rec.isOpen
                                      ? tr("Open")
                                      : rec.checkOut.toString("HH:mm"));
            setCell(COL_HOURS,    rec.isOpen
                                      ? "--"
                                      : QString::number(rec.hoursWorked, 'f', 2));
            setCell(COL_WAGE,     rec.isOpen
                                      ? "--"
                                      : QString::number(rec.dailyWage, 'f', 2));

            // Status column
            QString statusText;
            switch (rec.status) {
                case ImportHelper::ParsedRecord::Status::Clean:
                    statusText = rec.paid ? tr("Paid") : tr("Unpaid"); break;
                case ImportHelper::ParsedRecord::Status::SoftConflict:
                    statusText = tr("⚠ Conflict"); break;
                case ImportHelper::ParsedRecord::Status::HardError:
                    statusText = tr("✗ Error"); break;
            }
            auto* statusItem = setCell(COL_STATUS, statusText);
            if (rec.status == ImportHelper::ParsedRecord::Status::SoftConflict)
                statusItem->setForeground(QColor(ThemeHelper::isDark() ? "#ffb74d" : "#E65100"));
            else if (rec.status == ImportHelper::ParsedRecord::Status::HardError)
                statusItem->setForeground(QColor(ThemeHelper::isDark() ? "#ef9a9a" : "#C62828"));

            // Issue column
            auto* issueItem = setCell(COL_ISSUE, rec.issueDescription);
            issueItem->setToolTip(rec.issueDescription);
        }
    }

    m_updatingChecks = false;
    m_table->resizeRowsToContents();
    updateImportButton();
}

void ImportPreviewDialog::rebuildTable(bool conflictsOnly)
{
    populateTable(conflictsOnly);
}

// ── Slots ──────────────────────────────────────────────────────────────────

void ImportPreviewDialog::onFilterToggled(bool conflictsOnly)
{
    rebuildTable(conflictsOnly);
}

void ImportPreviewDialog::onSelectAllToggled(bool checked)
{
    if (m_updatingChecks) return;
    m_updatingChecks = true;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* item = m_table->item(row, COL_CHECK);
        if (!item) continue;
        // Only toggle items that are enabled (not hard errors)
        if (item->flags() & Qt::ItemIsUserCheckable)
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }

    m_updatingChecks = false;
    updateImportButton();
}

void ImportPreviewDialog::onRowCheckChanged(int row, int col)
{
    if (m_updatingChecks || col != COL_CHECK) return;
    if (row < 0 || row >= m_rowMap.size()) return;

    const auto& mapping = m_rowMap[row];
    if (mapping.empIndex < 0 || mapping.recIndex < 0) return;
    if (mapping.empIndex >= m_pass1.employees.size()) return;
    if (mapping.recIndex >= m_pass1.employees[mapping.empIndex].records.size()) return;

    auto* item = m_table->item(row, COL_CHECK);
    if (!item) return;

    // Write selection back to pass1
    m_pass1.employees[mapping.empIndex]
           .records[mapping.recIndex]
           .selected = (item->checkState() == Qt::Checked);

    updateImportButton();
}

void ImportPreviewDialog::onWageDecisionChanged(int empIndex, int decision)
{
    using WD = ImportHelper::ParsedEmployee::WageDecision;
    auto& emp = m_pass1.employees[empIndex];
    switch (decision) {
        case 0: emp.wageDecision = WD::RecalculateCurrent; break;
        case 1: emp.wageDecision = WD::KeepCsvValues;      break;
        case 2:
            emp.wageDecision = WD::CreateNew;
            emp.resolution   = ImportHelper::ParsedEmployee::Resolution::CreateNew;
            break;
    }
    updateImportButton();
}

void ImportPreviewDialog::onEmployeeResolutionChanged(int empIndex, int decision)
{
    using Res = ImportHelper::ParsedEmployee::Resolution;
    auto& emp = m_pass1.employees[empIndex];
    emp.resolution = (decision == 0) ? Res::CreateNew : Res::Skip;

    // When skipped, deselect all their records
    if (emp.resolution == Res::Skip) {
        for (auto& r : emp.records)
            r.selected = false;
        // Refresh table to reflect deselection
        rebuildTable(m_filterCheck->isChecked());
    }
    updateImportButton();
}

void ImportPreviewDialog::onImportClicked()
{
    accept();
}

void ImportPreviewDialog::updateImportButton()
{
    if (!m_importBtn) return;   // called during buildTable() before button is created
    const int n = m_pass1.totalSelectedCount();
    m_importBtn->setText(tr("Import Selected (%1)").arg(n));
    m_importBtn->setEnabled(n > 0);
}
