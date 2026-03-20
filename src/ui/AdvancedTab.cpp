#include "ui/AdvancedTab.h"
#include "ui/dialogs/PayrollRuleDialog.h"
#include "repositories/PayrollRulesRepository.h"
#include "repositories/EmployeeRepository.h"
#include "repositories/DayExceptionRepository.h"
#include "utils/CurrencyManager.h"
#include "utils/DeductionPolicy.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QTableWidgetItem>
#include <QDialog>
#include <QDateEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDate>
#include <QFrame>
#include <QCoreApplication>

static constexpr char kSettingsKey[] = "payroll_rules_enabled";

// ── Static helper ─────────────────────────────────────────────────────────

bool AdvancedTab::isPayrollRulesEnabled() {
    QSettings s("FakyLab", "Rawatib");
    return s.value(kSettingsKey, false).toBool();
}

// ── Constructor ───────────────────────────────────────────────────────────

AdvancedTab::AdvancedTab(QWidget* parent) : QWidget(parent) {
    setupUi();
    loadRulesAllTabs();
    loadExceptions();
    loadDeductionPolicy();
    const bool enabled = isPayrollRulesEnabled();
    m_masterCheck->setChecked(enabled);
    setControlsEnabled(enabled);
}

// ── Helper: build one rules table ─────────────────────────────────────────

static QTableWidget* makeRulesTable(QWidget* parent) {
    auto* t = new QTableWidget(parent);
    t->setColumnCount(4);
    t->setHorizontalHeaderLabels({
        QCoreApplication::translate("AdvancedTab", "Name"),
        QCoreApplication::translate("AdvancedTab", "Type"),
        QCoreApplication::translate("AdvancedTab", "Value"),
        QCoreApplication::translate("AdvancedTab", "Active")
    });
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    return t;
}

// ── Helper: build Add/Edit/Delete button row ──────────────────────────────

static QHBoxLayout* makeRuleBtnRow(QWidget* parent,
                                   QPushButton*& addBtn,
                                   QPushButton*& editBtn,
                                   QPushButton*& deleteBtn)
{
    auto* row  = new QHBoxLayout();
    addBtn    = new QPushButton(QCoreApplication::translate("AdvancedTab", "Add Rule"),    parent);
    editBtn   = new QPushButton(QCoreApplication::translate("AdvancedTab", "Edit Rule"),   parent);
    deleteBtn = new QPushButton(QCoreApplication::translate("AdvancedTab", "Delete Rule"), parent);
    editBtn->setEnabled(false);
    deleteBtn->setEnabled(false);
    row->addWidget(addBtn);
    row->addWidget(editBtn);
    row->addWidget(deleteBtn);
    row->addStretch();
    return row;
}

// ── setupUi ───────────────────────────────────────────────────────────────

void AdvancedTab::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ── Master toggle — above tabs, always visible ────────────────────────
    m_masterCheck = new QCheckBox(
        tr("Enable Payroll Rules  (deductions & additions)"), this);
    m_masterCheck->setToolTip(
        tr("When enabled, net pay is calculated from gross pay by applying\n"
           "the rules below. Disable to use gross pay as the final amount."));
    mainLayout->addWidget(m_masterCheck);

    // ── Tab widget ────────────────────────────────────────────────────────
    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    // ════════════════════════════════════════════════════════════════
    // Tab 0 — All Employees
    // ════════════════════════════════════════════════════════════════
    auto* allWidget = new QWidget();
    auto* allLayout = new QVBoxLayout(allWidget);
    allLayout->setSpacing(8);

    auto* allInfo = new QLabel(
        tr("Rules here apply to all employees regardless of pay type."), allWidget);
    allInfo->setWordWrap(true);
    QPalette grayPal = allInfo->palette();
    grayPal.setColor(QPalette::WindowText, allInfo->palette().color(QPalette::Mid));
    allInfo->setPalette(grayPal);
    allLayout->addWidget(allInfo);

    m_tableAll = makeRulesTable(allWidget);
    allLayout->addWidget(m_tableAll);
    allLayout->addLayout(
        makeRuleBtnRow(allWidget, m_addBtnAll, m_editBtnAll, m_deleteBtnAll));

    // Day Exceptions — lives in All Employees tab
    auto* exSep = new QFrame(allWidget);
    exSep->setFrameShape(QFrame::HLine);
    exSep->setFrameShadow(QFrame::Sunken);
    allLayout->addWidget(exSep);

    auto* exGroup  = new QGroupBox(tr("Day Exceptions  (holidays & approved leave)"), allWidget);
    auto* exLayout = new QVBoxLayout(exGroup);

    auto* exDesc = new QLabel(
        tr("Dates listed here are not counted as absent days for monthly salary employees. "
           "Use this for public holidays, company closures, or individual approved leave. "
           "Select 'All Employees' to apply company-wide."), exGroup);
    exDesc->setWordWrap(true);
    QPalette exPal = exDesc->palette();
    exPal.setColor(QPalette::WindowText, exDesc->palette().color(QPalette::Mid));
    exDesc->setPalette(exPal);
    exLayout->addWidget(exDesc);

    m_exTable = new QTableWidget(allWidget);
    m_exTable->setColumnCount(3);
    m_exTable->setHorizontalHeaderLabels({tr("Date"), tr("Scope"), tr("Reason")});
    m_exTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_exTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_exTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_exTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_exTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_exTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_exTable->setAlternatingRowColors(true);
    m_exTable->verticalHeader()->setVisible(false);
    m_exTable->setMaximumHeight(160);
    exLayout->addWidget(m_exTable);

    auto* exBtnRow = new QHBoxLayout();
    m_exAddBtn    = new QPushButton(tr("Add Exception"), allWidget);
    m_exRemoveBtn = new QPushButton(tr("Remove"),        allWidget);
    m_exRemoveBtn->setEnabled(false);
    exBtnRow->addWidget(m_exAddBtn);
    exBtnRow->addWidget(m_exRemoveBtn);
    exBtnRow->addStretch();
    exLayout->addLayout(exBtnRow);

    allLayout->addWidget(exGroup);
    m_tabWidget->addTab(allWidget, tr("All Employees"));

    // ════════════════════════════════════════════════════════════════
    // Tab 1 — Monthly Employees
    // ════════════════════════════════════════════════════════════════
    auto* monthlyWidget = new QWidget();
    auto* monthlyLayout = new QVBoxLayout(monthlyWidget);
    monthlyLayout->setSpacing(8);

    // Deduction policy group — moved from top level into this tab
    m_deductionGroup = new QGroupBox(
        tr("Monthly Salary — Late/Early Deduction Mode"), monthlyWidget);
    auto* dedLayout = new QVBoxLayout(m_deductionGroup);
    dedLayout->setSpacing(6);

    m_radioPerMinute = new QRadioButton(
        tr("Per-minute  —  deduct proportionally for each minute late or early"), monthlyWidget);
    m_radioPerDay    = new QRadioButton(
        tr("Per-day penalty  —  deduct a fixed % of daily rate when threshold is exceeded"), monthlyWidget);
    m_radioOff       = new QRadioButton(
        tr("Off  —  no automatic deduction (use payroll rules manually)"), monthlyWidget);

    dedLayout->addWidget(m_radioPerMinute);
    dedLayout->addWidget(m_radioPerDay);

    auto* penaltyRow = new QHBoxLayout();
    m_penaltyLabel   = new QLabel(tr("Penalty per occurrence:"), monthlyWidget);
    m_penaltySpin    = new QDoubleSpinBox(monthlyWidget);
    m_penaltySpin->setRange(1.0, 100.0);
    m_penaltySpin->setDecimals(1);
    m_penaltySpin->setSingleStep(5.0);
    m_penaltySpin->setSuffix(" %");
    m_penaltySpin->setFixedWidth(90);
    m_penaltySpin->setToolTip(
        tr("Percentage of the employee's daily rate deducted per occurrence.\n"
           "Late arrival and early departure are counted independently.\n"
           "Example: 50% means arriving late costs half a day's wage."));
    auto* penaltyHint = new QLabel(
        tr("of daily rate per late arrival / early departure"), monthlyWidget);
    penaltyHint->setEnabled(false);
    penaltyRow->addSpacing(24);
    penaltyRow->addWidget(m_penaltyLabel);
    penaltyRow->addWidget(m_penaltySpin);
    penaltyRow->addWidget(penaltyHint);
    penaltyRow->addStretch();
    dedLayout->addLayout(penaltyRow);
    dedLayout->addWidget(m_radioOff);
    monthlyLayout->addWidget(m_deductionGroup);

    auto* monthlyInfo = new QLabel(
        tr("Rules here apply to monthly salary employees only."), monthlyWidget);
    monthlyInfo->setWordWrap(true);
    QPalette mPal = monthlyInfo->palette();
    mPal.setColor(QPalette::WindowText, monthlyInfo->palette().color(QPalette::Mid));
    monthlyInfo->setPalette(mPal);
    monthlyLayout->addWidget(monthlyInfo);

    m_tableMonthly = makeRulesTable(monthlyWidget);
    monthlyLayout->addWidget(m_tableMonthly);
    monthlyLayout->addLayout(
        makeRuleBtnRow(monthlyWidget, m_addBtnMonthly, m_editBtnMonthly, m_deleteBtnMonthly));

    m_tabWidget->addTab(monthlyWidget, tr("Monthly Employees"));

    // ════════════════════════════════════════════════════════════════
    // Tab 2 — Hourly Employees
    // ════════════════════════════════════════════════════════════════
    auto* hourlyWidget = new QWidget();
    auto* hourlyLayout = new QVBoxLayout(hourlyWidget);
    hourlyLayout->setSpacing(8);

    auto* hourlyInfo = new QLabel(
        tr("Rules here apply to hourly employees only."), hourlyWidget);
    hourlyInfo->setWordWrap(true);
    QPalette hPal = hourlyInfo->palette();
    hPal.setColor(QPalette::WindowText, hourlyInfo->palette().color(QPalette::Mid));
    hourlyInfo->setPalette(hPal);
    hourlyLayout->addWidget(hourlyInfo);

    m_tableHourly = makeRulesTable(hourlyWidget);
    hourlyLayout->addWidget(m_tableHourly);
    hourlyLayout->addLayout(
        makeRuleBtnRow(hourlyWidget, m_addBtnHourly, m_editBtnHourly, m_deleteBtnHourly));

    m_tabWidget->addTab(hourlyWidget, tr("Hourly Employees"));

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_masterCheck, &QCheckBox::toggled,
            this, &AdvancedTab::onMasterToggled);

    // Rules buttons — all three tabs connect to same slots
    connect(m_addBtnAll,       &QPushButton::clicked, this, &AdvancedTab::onAddRule);
    connect(m_editBtnAll,      &QPushButton::clicked, this, &AdvancedTab::onEditRule);
    connect(m_deleteBtnAll,    &QPushButton::clicked, this, &AdvancedTab::onDeleteRule);
    connect(m_tableAll,        &QTableWidget::itemSelectionChanged,
            this, &AdvancedTab::onSelectionChanged);
    connect(m_tableAll,        &QTableWidget::itemDoubleClicked,
            this, &AdvancedTab::onEditRule);

    connect(m_addBtnMonthly,    &QPushButton::clicked, this, &AdvancedTab::onAddRule);
    connect(m_editBtnMonthly,   &QPushButton::clicked, this, &AdvancedTab::onEditRule);
    connect(m_deleteBtnMonthly, &QPushButton::clicked, this, &AdvancedTab::onDeleteRule);
    connect(m_tableMonthly,     &QTableWidget::itemSelectionChanged,
            this, &AdvancedTab::onSelectionChanged);
    connect(m_tableMonthly,     &QTableWidget::itemDoubleClicked,
            this, &AdvancedTab::onEditRule);

    connect(m_addBtnHourly,    &QPushButton::clicked, this, &AdvancedTab::onAddRule);
    connect(m_editBtnHourly,   &QPushButton::clicked, this, &AdvancedTab::onEditRule);
    connect(m_deleteBtnHourly, &QPushButton::clicked, this, &AdvancedTab::onDeleteRule);
    connect(m_tableHourly,     &QTableWidget::itemSelectionChanged,
            this, &AdvancedTab::onSelectionChanged);
    connect(m_tableHourly,     &QTableWidget::itemDoubleClicked,
            this, &AdvancedTab::onEditRule);

    // Day exceptions
    connect(m_exAddBtn,    &QPushButton::clicked,
            this, &AdvancedTab::onAddException);
    connect(m_exRemoveBtn, &QPushButton::clicked,
            this, &AdvancedTab::onRemoveException);
    connect(m_exTable,     &QTableWidget::itemSelectionChanged,
            this, &AdvancedTab::onExceptionSelectionChanged);

    // Deduction policy
    connect(m_radioPerMinute, &QRadioButton::toggled,
            this, &AdvancedTab::onDeductionModeChanged);
    connect(m_radioPerDay,    &QRadioButton::toggled,
            this, &AdvancedTab::onDeductionModeChanged);
    connect(m_radioOff,       &QRadioButton::toggled,
            this, &AdvancedTab::onDeductionModeChanged);
    connect(m_penaltySpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &AdvancedTab::onPenaltyPctChanged);
}

// ── Active tab helpers ────────────────────────────────────────────────────

PayrollRule::AppliesTo AdvancedTab::activeTabAppliesTo() const {
    switch (m_tabWidget->currentIndex()) {
        case 1:  return PayrollRule::AppliesTo::Monthly;
        case 2:  return PayrollRule::AppliesTo::Hourly;
        default: return PayrollRule::AppliesTo::All;
    }
}

QTableWidget* AdvancedTab::activeRulesTable() const {
    switch (m_tabWidget->currentIndex()) {
        case 1:  return m_tableMonthly;
        case 2:  return m_tableHourly;
        default: return m_tableAll;
    }
}

QPushButton* AdvancedTab::activeEditBtn() const {
    switch (m_tabWidget->currentIndex()) {
        case 1:  return m_editBtnMonthly;
        case 2:  return m_editBtnHourly;
        default: return m_editBtnAll;
    }
}

QPushButton* AdvancedTab::activeDeleteBtn() const {
    switch (m_tabWidget->currentIndex()) {
        case 1:  return m_deleteBtnMonthly;
        case 2:  return m_deleteBtnHourly;
        default: return m_deleteBtnAll;
    }
}

// ── Master toggle ─────────────────────────────────────────────────────────

void AdvancedTab::onMasterToggled(bool enabled) {
    QSettings s("FakyLab", "Rawatib");
    s.setValue(kSettingsKey, enabled);
    s.sync();
    setControlsEnabled(enabled);
    emit rulesChanged();
}

void AdvancedTab::setControlsEnabled(bool enabled) {
    m_tabWidget->setEnabled(enabled);
}

// ── Load rules ────────────────────────────────────────────────────────────

void AdvancedTab::loadRulesAllTabs() {
    loadRulesForTable(m_tableAll,     PayrollRule::AppliesTo::All);
    loadRulesForTable(m_tableMonthly, PayrollRule::AppliesTo::Monthly);
    loadRulesForTable(m_tableHourly,  PayrollRule::AppliesTo::Hourly);
}

void AdvancedTab::loadRulesForTable(QTableWidget* table,
                                     PayrollRule::AppliesTo tab) {
    const auto rules = PayrollRulesRepository::instance().getRulesForTab(tab);
    table->setRowCount(0);

    for (const auto& r : rules) {
        const int row = table->rowCount();
        table->insertRow(row);

        auto cell = [&](const QString& text,
                        Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            return item;
        };

        auto* nameItem = new QTableWidgetItem(r.name);
        nameItem->setData(Qt::UserRole, r.id);
        table->setItem(row, 0, nameItem);

        const QString typeStr = (r.type == PayrollRule::Type::Deduction)
            ? tr("Deduction") : tr("Addition");
        table->setItem(row, 1, cell(typeStr, Qt::AlignVCenter | Qt::AlignHCenter));

        QString valueStr;
        if (r.basis == PayrollRule::Basis::PercentOfGross)
            valueStr = QString("%1 %").arg(r.value, 0, 'f', 2);
        else
            valueStr = CurrencyManager::format(r.value);
        table->setItem(row, 2, cell(valueStr, Qt::AlignVCenter | Qt::AlignHCenter));

        const QString activeStr = r.enabled ? tr("Yes") : tr("No");
        auto* activeItem = cell(activeStr, Qt::AlignVCenter | Qt::AlignHCenter);
        if (!r.enabled)
            activeItem->setForeground(QColor("#999999"));
        table->setItem(row, 3, activeItem);
        table->setRowHeight(row, 26);
    }
}

// ── Selection ─────────────────────────────────────────────────────────────

void AdvancedTab::onSelectionChanged() {
    const bool selected = !activeRulesTable()->selectedItems().isEmpty();
    activeEditBtn()->setEnabled(selected);
    activeDeleteBtn()->setEnabled(selected);
}

// ── CRUD ──────────────────────────────────────────────────────────────────

void AdvancedTab::onAddRule() {
    PayrollRuleDialog dlg(activeTabAppliesTo(), this);
    if (dlg.exec() == QDialog::Accepted) {
        PayrollRule rule = dlg.rule();
        rule.sortOrder = activeRulesTable()->rowCount();
        if (PayrollRulesRepository::instance().addRule(rule)) {
            loadRulesAllTabs();
            emit rulesChanged();
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("Failed to save rule:\n%1")
                    .arg(PayrollRulesRepository::instance().lastError()));
        }
    }
}

void AdvancedTab::onEditRule() {
    auto* table = activeRulesTable();
    const auto rows = table->selectedItems();
    if (rows.isEmpty()) return;

    const int ruleId = table->item(rows.first()->row(), 0)
                           ->data(Qt::UserRole).toInt();

    const auto allRules = PayrollRulesRepository::instance().getAllRules();
    for (const auto& r : allRules) {
        if (r.id == ruleId) {
            PayrollRuleDialog dlg(r, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (PayrollRulesRepository::instance().updateRule(dlg.rule())) {
                    loadRulesAllTabs();   // reload all tabs — rule may move between them
                    emit rulesChanged();
                } else {
                    QMessageBox::critical(this, tr("Error"),
                        tr("Failed to update rule:\n%1")
                            .arg(PayrollRulesRepository::instance().lastError()));
                }
            }
            return;
        }
    }
}

void AdvancedTab::onDeleteRule() {
    auto* table = activeRulesTable();
    const auto rows = table->selectedItems();
    if (rows.isEmpty()) return;

    const int ruleId = table->item(rows.first()->row(), 0)
                           ->data(Qt::UserRole).toInt();
    const QString name = table->item(rows.first()->row(), 0)->text();

    const auto reply = QMessageBox::question(this, tr("Confirm Delete"),
        tr("Delete rule \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (PayrollRulesRepository::instance().deleteRule(ruleId)) {
            loadRulesAllTabs();
            emit rulesChanged();
        }
    }
}

// ── Day Exceptions ────────────────────────────────────────────────────────

void AdvancedTab::onEmployeeListChanged() {
    // Nothing cached — the Add dialog fetches fresh from repo each time
}

void AdvancedTab::loadExceptions() {
    const auto exceptions = DayExceptionRepository::instance().getAll();
    m_exTable->blockSignals(true);
    m_exTable->setRowCount(0);

    for (const auto& ex : exceptions) {
        const int row = m_exTable->rowCount();
        m_exTable->insertRow(row);

        auto* dateItem = new QTableWidgetItem(ex.date.toString("yyyy-MM-dd"));
        dateItem->setData(Qt::UserRole, ex.id);
        m_exTable->setItem(row, 0, dateItem);

        QString scope;
        if (ex.employeeId <= 0) {
            scope = tr("All Employees");
        } else {
            auto emp = EmployeeRepository::instance().getEmployee(ex.employeeId);
            scope = emp ? emp->name : tr("Employee #%1").arg(ex.employeeId);
        }
        m_exTable->setItem(row, 1, new QTableWidgetItem(scope));
        m_exTable->setItem(row, 2, new QTableWidgetItem(ex.reason));
        m_exTable->setRowHeight(row, 24);
    }

    m_exTable->blockSignals(false);
    m_exRemoveBtn->setEnabled(false);
}

void AdvancedTab::onExceptionSelectionChanged() {
    m_exRemoveBtn->setEnabled(!m_exTable->selectedItems().isEmpty());
}

void AdvancedTab::onAddException() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Day Exception"));
    dlg.setMinimumWidth(320);
    auto* layout = new QVBoxLayout(&dlg);
    auto* form   = new QFormLayout();

    auto* dateEdit = new QDateEdit(QDate::currentDate(), &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    form->addRow(tr("Date:"), dateEdit);

    auto* scopeCombo = new QComboBox(&dlg);
    scopeCombo->addItem(tr("All Employees"), 0);
    const auto employees = EmployeeRepository::instance().getAllEmployees();
    for (const auto& emp : employees)
        scopeCombo->addItem(emp.name, emp.id);
    form->addRow(tr("Applies to:"), scopeCombo);

    auto* reasonEdit = new QLineEdit(&dlg);
    reasonEdit->setPlaceholderText(tr("e.g. Eid Al-Fitr, National Day"));
    form->addRow(tr("Reason:"), reasonEdit);

    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    DayException ex;
    ex.date       = dateEdit->date();
    ex.employeeId = scopeCombo->currentData().toInt();
    ex.reason     = reasonEdit->text().trimmed();

    if (!DayExceptionRepository::instance().addException(ex)) {
        const QString err = DayExceptionRepository::instance().lastError();
        if (!err.isEmpty())
            QMessageBox::warning(this, tr("Error"),
                tr("Could not add exception:\n%1").arg(err));
    }
    loadExceptions();
}

void AdvancedTab::onRemoveException() {
    const auto rows = m_exTable->selectedItems();
    if (rows.isEmpty()) return;

    const int exId = m_exTable->item(rows.first()->row(), 0)
                         ->data(Qt::UserRole).toInt();
    const QString dateStr = m_exTable->item(rows.first()->row(), 0)->text();
    const QString scope   = m_exTable->item(rows.first()->row(), 1)->text();

    const auto reply = QMessageBox::question(this, tr("Remove Exception"),
        tr("Remove exception for %1 (%2)?").arg(dateStr).arg(scope),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        DayExceptionRepository::instance().removeException(exId);
        loadExceptions();
    }
}

// ── Deduction policy ──────────────────────────────────────────────────────

void AdvancedTab::loadDeductionPolicy() {
    const DeductionPolicy::Mode m = DeductionPolicy::mode();
    const double pct              = DeductionPolicy::perDayPenaltyPct();

    m_radioPerMinute->blockSignals(true);
    m_radioPerDay->blockSignals(true);
    m_radioOff->blockSignals(true);
    m_penaltySpin->blockSignals(true);

    m_radioPerMinute->setChecked(m == DeductionPolicy::Mode::PerMinute);
    m_radioPerDay->setChecked(m    == DeductionPolicy::Mode::PerDay);
    m_radioOff->setChecked(m       == DeductionPolicy::Mode::Off);
    m_penaltySpin->setValue(pct);

    const bool perDay = (m == DeductionPolicy::Mode::PerDay);
    m_penaltySpin->setEnabled(perDay);
    m_penaltyLabel->setEnabled(perDay);

    m_radioPerMinute->blockSignals(false);
    m_radioPerDay->blockSignals(false);
    m_radioOff->blockSignals(false);
    m_penaltySpin->blockSignals(false);
}

void AdvancedTab::onDeductionModeChanged() {
    DeductionPolicy::Mode newMode = DeductionPolicy::Mode::PerMinute;
    if (m_radioPerDay->isChecked())
        newMode = DeductionPolicy::Mode::PerDay;
    else if (m_radioOff->isChecked())
        newMode = DeductionPolicy::Mode::Off;

    const bool perDay = (newMode == DeductionPolicy::Mode::PerDay);
    m_penaltySpin->setEnabled(perDay);
    m_penaltyLabel->setEnabled(perDay);

    if (!qobject_cast<QRadioButton*>(sender())->isChecked()) return;

    const DeductionPolicy::Mode oldMode = DeductionPolicy::mode();
    if (oldMode != newMode) {
        QMessageBox::information(this,
            tr("Deduction Mode Changed"),
            tr("This change applies to new check-outs only.\n"
               "Existing attendance records are not recalculated."));
    }

    DeductionPolicy::setMode(newMode);
}

void AdvancedTab::onPenaltyPctChanged(double pct) {
    DeductionPolicy::setPenaltyPct(pct);
}
