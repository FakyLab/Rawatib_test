#include "ui/SalaryTab.h"
#include "utils/ThemeHelper.h"
#include "repositories/AttendanceRepository.h"
#include "repositories/EmployeeRepository.h"
#include "repositories/PayrollRulesRepository.h"
#include "utils/PrintHelper.h"
#include "utils/ExportHelper.h"
#include "utils/CurrencyManager.h"
#include "utils/PayrollCalculator.h"
#include "utils/LockPolicy.h"
#include "utils/EmployeePinManager.h"
#include "utils/PinManager.h"
#include "utils/SessionManager.h"
#include "ui/AdvancedTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDate>
#include <QLocale>
#include <QGuiApplication>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QDialog>
#include <QRegularExpressionValidator>

static inline QLocale appLocale() {
    return QGuiApplication::layoutDirection() == Qt::RightToLeft
               ? QLocale(QLocale::Arabic) : QLocale::c();
}

SalaryTab::SalaryTab(QWidget* parent) : QWidget(parent) {
    m_adminUnlocked = !PinManager::isPinSet();
    m_year  = QDate::currentDate().year();
    m_month = QDate::currentDate().month();
    setupUi();
}

void SalaryTab::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // ── Period filter ──────────────────────────────────────────────────────
    auto* filterRow = new QHBoxLayout();
    filterRow->addWidget(new QLabel(tr("Period:"), this));

    m_monthCombo = new QComboBox(this);
    for (int m = 1; m <= 12; ++m)
        m_monthCombo->addItem(appLocale().monthName(m), m);
    m_monthCombo->setCurrentIndex(m_month - 1);

    m_yearCombo = new QComboBox(this);
    const int curYear      = QDate::currentDate().year();
    const int earliestYear = AttendanceRepository::instance().getEarliestRecordYear();
    const int fromYear     = qMin(earliestYear, curYear - 1);
    for (int y = fromYear; y <= curYear + 1; ++y)
        m_yearCombo->addItem(QString::number(y), y);
    m_yearCombo->setCurrentText(QString::number(m_year));

    filterRow->addWidget(m_monthCombo);
    filterRow->addWidget(m_yearCombo);

    // ── "Show My Wages" self-view checkbox ────────────────────────────────
    m_selfViewCheck = new QCheckBox(tr("Show My Wages"), this);
    m_selfViewCheck->setVisible(false);
    m_selfViewCheck->setStyleSheet(
        "QCheckBox { color: palette(mid); }"
        "QCheckBox:hover { color: palette(text); }"
        "QCheckBox::indicator { width: 13px; height: 13px; }");
    m_selfViewCheck->setToolTip(
        tr("Enter your PIN to view your own wages.\n"
           "Resets automatically after 2 minutes of inactivity."));
    filterRow->addWidget(m_selfViewCheck);
    connect(m_selfViewCheck, &QCheckBox::toggled,
            this, &SalaryTab::onSelfViewToggled);

    filterRow->addStretch();

    // ── Lock button (only visible when HideWages is active) ───────────────
    m_lockBtn = new QPushButton(this);
    m_lockBtn->setFlat(true);
    m_lockBtn->setFixedSize(28, 28);
    m_lockBtn->setVisible(PinManager::isPinSet()
                       && LockPolicy::isLocked(LockPolicy::Feature::HideWages));
    filterRow->addWidget(m_lockBtn);
    refreshLockIcon();
    connect(m_lockBtn, &QPushButton::clicked,
            this, &SalaryTab::lockIconClicked);

    m_selfViewTimer = new QTimer(this);
    m_selfViewTimer->setSingleShot(true);
    m_selfViewTimer->setInterval(2 * 60 * 1000);
    connect(m_selfViewTimer, &QTimer::timeout,
            this, &SalaryTab::onSelfViewTimeout);

    mainLayout->addLayout(filterRow);

    // ── Gross summary group ────────────────────────────────────────────────
    auto* summaryGroup = new QGroupBox(tr("Monthly Summary"), this);
    auto* grid = new QGridLayout(summaryGroup);
    grid->setSpacing(8);

    auto addRow = [&](int row, const QString& label, QLabel*& labelWidget, QLabel*& valueLabel) {
        labelWidget = new QLabel(label, summaryGroup);
        grid->addWidget(labelWidget, row, 0);
        valueLabel = new QLabel("--", summaryGroup);
        grid->addWidget(valueLabel, row, 1);
    };
    // Overload for rows that don't need a label pointer stored
    auto addRowV = [&](int row, const QString& label, QLabel*& valueLabel) {
        grid->addWidget(new QLabel(label, summaryGroup), row, 0);
        valueLabel = new QLabel("--", summaryGroup);
        grid->addWidget(valueLabel, row, 1);
    };

    addRowV(0, tr("Total Days Worked:"),  m_totalDaysValue);
    addRowV(1, tr("Total Hours Worked:"), m_totalHoursValue);
    addRowV(2, tr("Total Salary:"),       m_totalSalaryValue);
    addRowV(3, tr("Paid Amount:"),        m_paidAmountValue);
    addRowV(4, tr("Unpaid Remaining:"),   m_unpaidAmountValue);

    // Monthly-only rows — hidden by default, shown when employee is monthly
    addRow(5, tr("Expected Salary:"),   m_expectedSalaryLabel,  m_expectedSalaryValue);
    addRow(6, tr("Days Off (Approved):"),m_exceptionDaysLabel,   m_exceptionDaysValue);
    addRow(7, tr("Days Absent:"),       m_absentDaysLabel,      m_absentDaysValue);
    addRow(8, tr("Total Deductions:"),  m_totalDeductionsLabel, m_totalDeductionsValue);
    m_expectedSalaryLabel->setVisible(false);  m_expectedSalaryValue->setVisible(false);
    m_exceptionDaysLabel->setVisible(false);   m_exceptionDaysValue->setVisible(false);
    m_absentDaysLabel->setVisible(false);      m_absentDaysValue->setVisible(false);
    m_totalDeductionsLabel->setVisible(false); m_totalDeductionsValue->setVisible(false);

    mainLayout->addWidget(summaryGroup);

    // ── Net pay section ────────────────────────────────────────────────────
    // Hidden by default — shown only when payroll rules are enabled.
    m_netGroup = new QGroupBox(tr("Net Pay  (after deductions & additions)"), this);
    auto* netLayout = new QVBoxLayout(m_netGroup);
    netLayout->setSpacing(6);

    // Inline-editable rules table
    m_rulesTable = new QTableWidget(this);
    m_rulesTable->setColumnCount(3);
    m_rulesTable->setHorizontalHeaderLabels({
        tr("Rule"), tr("Type"), tr("Value")
    });
    m_rulesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_rulesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_rulesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_rulesTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_rulesTable->setAlternatingRowColors(true);
    m_rulesTable->verticalHeader()->setVisible(false);
    m_rulesTable->setMaximumHeight(160);
    netLayout->addWidget(m_rulesTable);

    // Net pay total row
    auto* netRow = new QHBoxLayout();
    netRow->addStretch();
    netRow->addWidget(new QLabel(tr("Net Pay:"), this));
    m_netPayValue = new QLabel("--", this);
    QFont boldFont = m_netPayValue->font();
    boldFont.setBold(true);
    boldFont.setPointSize(boldFont.pointSize() + 1);
    m_netPayValue->setFont(boldFont);
    netRow->addWidget(m_netPayValue);
    netLayout->addLayout(netRow);

    mainLayout->addWidget(m_netGroup);
    m_netGroup->setVisible(false);   // hidden until feature is enabled

    // ── Buttons ────────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_printBtn  = new QPushButton(tr("Print Report"), this);
    m_exportBtn = new QPushButton(tr("Export..."),    this);
    m_printBtn->setEnabled(false);
    m_exportBtn->setEnabled(false);
    m_exportBtn->setToolTip(
        tr("Export attendance data to Excel (.xlsx) or CSV (.csv)"));
    btnRow->addWidget(m_printBtn);
    btnRow->addWidget(m_exportBtn);
    mainLayout->addLayout(btnRow);

    mainLayout->addStretch();

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_monthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SalaryTab::onMonthChanged);
    connect(m_yearCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SalaryTab::onMonthChanged);
    connect(m_printBtn,  &QPushButton::clicked, this, &SalaryTab::onPrint);
    connect(m_exportBtn, &QPushButton::clicked, this, &SalaryTab::onExport);
}

// ── Public interface ──────────────────────────────────────────────────────

void SalaryTab::setEmployee(int employeeId, const QString& employeeName) {
    m_employeeId   = employeeId;
    m_employeeName = employeeName;
    m_printBtn->setEnabled(employeeId > 0);
    m_exportBtn->setEnabled(employeeId > 0);

    // Cache pay type for payroll rule filtering
    if (employeeId > 0) {
        auto emp = EmployeeRepository::instance().getEmployee(employeeId);
        m_employeePayType = emp ? emp->payType : PayType::Hourly;
    } else {
        m_employeePayType = PayType::Hourly;
    }

    // Reset self-view when switching employees
    m_selfViewActive = false;
    m_selfViewTimer->stop();
    if (m_selfViewCheck) {
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(false);
        m_selfViewCheck->blockSignals(false);
    }
    refreshSelfViewCheckbox();
    loadSummary();
}

void SalaryTab::setMonth(int year, int month) {
    m_year = year; m_month = month;
    m_yearCombo->blockSignals(true);
    m_monthCombo->blockSignals(true);
    m_yearCombo->setCurrentText(QString::number(year));
    m_monthCombo->setCurrentIndex(month - 1);
    m_yearCombo->blockSignals(false);
    m_monthCombo->blockSignals(false);
    loadSummary();
}

void SalaryTab::refresh() { loadSummary(); }

void SalaryTab::onRulesChanged() { loadSummary(); }

// ── Internal ──────────────────────────────────────────────────────────────

void SalaryTab::onMonthChanged() {
    m_month = m_monthCombo->currentData().toInt();
    m_year  = m_yearCombo->currentData().toInt();
    loadSummary();
    emit monthChanged(m_year, m_month);
}

void SalaryTab::loadSummary() {
    if (m_employeeId <= 0) {
        m_totalDaysValue->setText("--");
        m_totalHoursValue->setText("--");
        m_totalSalaryValue->setText("--");
        m_paidAmountValue->setText("--");
        m_unpaidAmountValue->setText("--");
        m_netGroup->setVisible(false);
        return;
    }
    updateSummaryCards();
    updateNetPaySection();
}

void SalaryTab::updateSummaryCards() {
    auto s = AttendanceRepository::instance().getMonthlySummary(
        m_employeeId, m_year, m_month);

    m_grossPay = s.totalSalary;

    const bool monthly = s.isMonthly;

    // "Total Hours Worked" is only meaningful for hourly employees
    m_totalHoursValue->parentWidget()->setVisible(true);   // keep row always
    if (monthly) {
        // For monthly: show days present out of working days
        const auto emp = EmployeeRepository::instance().getEmployee(m_employeeId);
        const int  wdays = emp ? emp->workingDaysPerMonth : 26;
        m_totalDaysValue->setText(
            tr("%1 / %2 days").arg(s.presentDays).arg(wdays));
        m_totalHoursValue->setText(
            QString("%1 %2").arg(s.totalHours, 0, 'f', 2).arg(tr("hrs")));
    } else {
        m_totalDaysValue->setText(
            QString("%1 %2").arg(s.totalDays).arg(tr("days")));
        m_totalHoursValue->setText(
            QString("%1 %2").arg(s.totalHours, 0, 'f', 2).arg(tr("hrs")));
    }

    if (wagesVisible()) {
        m_totalSalaryValue->setText(CurrencyManager::format(s.totalSalary));
        m_paidAmountValue->setText(CurrencyManager::format(s.paidAmount));
        m_unpaidAmountValue->setText(CurrencyManager::format(s.unpaidAmount));
    } else {
        m_totalSalaryValue->setText("--");
        m_paidAmountValue->setText("--");
        m_unpaidAmountValue->setText("--");
    }

    // Monthly-only rows
    m_expectedSalaryLabel->setVisible(monthly);
    m_expectedSalaryValue->setVisible(monthly);
    m_exceptionDaysLabel->setVisible(monthly);
    m_exceptionDaysValue->setVisible(monthly);
    m_absentDaysLabel->setVisible(monthly);
    m_absentDaysValue->setVisible(monthly);
    m_totalDeductionsLabel->setVisible(monthly);
    m_totalDeductionsValue->setVisible(monthly);

    if (monthly) {
        m_expectedSalaryValue->setText(
            wagesVisible() ? CurrencyManager::format(s.expectedSalary) : "--");
        // Exception days — show count, dimmed if zero
        if (s.exceptionDays > 0)
            m_exceptionDaysValue->setText(tr("%1 days").arg(s.exceptionDays));
        else
            m_exceptionDaysValue->setText(tr("None"));
        m_exceptionDaysValue->setStyleSheet(
            s.exceptionDays > 0 ? "" : "color: palette(mid);");
        m_absentDaysValue->setText(tr("%1 days").arg(s.absentDays));

        if (wagesVisible()) {
            // Annotate deductions with breakdown:
            //   absentDeduction  — days with no attendance record
            //   lateDeduction    — already baked into totalSalary, shown for info
            //   earlyDeduction   — already baked into totalSalary, shown for info
            QStringList parts;
            if (s.absentDeduction > 0)
                parts << tr("%1 absent").arg(CurrencyManager::format(s.absentDeduction));
            if (s.lateDeduction > 0)
                parts << tr("%1 late (%2 min)")
                         .arg(CurrencyManager::format(s.lateDeduction))
                         .arg(s.totalLateMinutes);
            if (s.earlyDeduction > 0)
                parts << tr("%1 early (%2 min)")
                         .arg(CurrencyManager::format(s.earlyDeduction))
                         .arg(s.totalEarlyMinutes);

            const QString dedText = CurrencyManager::format(s.totalDeductions)
                + (parts.isEmpty() ? "" : "  (" + parts.join(", ") + ")");
            m_totalDeductionsValue->setText(dedText);
        } else {
            m_totalDeductionsValue->setText("--");
        }
    }
}

void SalaryTab::updateNetPaySection() {
    const bool rulesEnabled = AdvancedTab::isPayrollRulesEnabled();
    m_netGroup->setVisible(rulesEnabled && wagesVisible());
    if (!rulesEnabled || !wagesVisible()) return;

    // Rebuild applied rules fresh from DB — filtered by this employee's pay type
    const auto enabledRules =
        PayrollRulesRepository::instance().getEnabledRulesForPayType(m_employeePayType);
    m_appliedRules = PayrollCalculator::buildAppliedRules(enabledRules);

    // Populate the rules table — block signals during fill
    m_rulesTable->blockSignals(true);
    m_rulesTable->setRowCount(0);

    for (int i = 0; i < m_appliedRules.size(); ++i) {
        const auto& ar = m_appliedRules[i];
        m_rulesTable->insertRow(i);

        // Col 0: Name — read-only
        auto* nameItem = new QTableWidgetItem(ar.rule.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_rulesTable->setItem(i, 0, nameItem);

        // Col 1: Type — read-only
        const QString typeStr =
            (ar.rule.type == PayrollRule::Type::Deduction)
                ? tr("Deduction") : tr("Addition");
        auto* typeItem = new QTableWidgetItem(typeStr);
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        typeItem->setTextAlignment(Qt::AlignCenter);
        m_rulesTable->setItem(i, 1, typeItem);

        // Col 2: Value — QDoubleSpinBox embedded directly in cell
        auto* spin = new QDoubleSpinBox(m_rulesTable);
        spin->setFrame(false);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setToolTip(tr("Edit to override this value for the current view.\n"
                            "To change the default, use the Advanced tab."));

        if (ar.rule.basis == PayrollRule::Basis::PercentOfGross) {
            spin->setRange(0.0, 100.0);
            spin->setDecimals(2);
            spin->setSuffix(" %");
            spin->setSingleStep(0.5);
        } else {
            spin->setRange(0.0, 999999.99);
            spin->setDecimals(2);
            spin->setSingleStep(1.0);
        }
        spin->setValue(ar.appliedValue);

        // Recalculate net pay live whenever the user changes a value
        const int idx = i;
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, idx](double v) {
            if (idx < m_appliedRules.size()) {
                m_appliedRules[idx].appliedValue = v;
                const auto res = PayrollCalculator::calculate(
                    m_grossPay, m_appliedRules);
                m_netPayValue->setText(CurrencyManager::format(res.netPay));
            }
        });

        m_rulesTable->setCellWidget(i, 2, spin);
        m_rulesTable->setRowHeight(i, 28);
    }

    m_rulesTable->blockSignals(false);

    if (m_appliedRules.isEmpty()) {
        m_netPayValue->setText(
            tr("No active rules — add rules in the Advanced tab"));
        return;
    }

    // Initial net pay
    const auto res = PayrollCalculator::calculate(m_grossPay, m_appliedRules);
    m_netPayValue->setText(CurrencyManager::format(res.netPay));
}

void SalaryTab::onRuleValueChanged(int /*row*/, int /*col*/) {}

// ── Print / Export ────────────────────────────────────────────────────────

void SalaryTab::onPrint() {
    if (m_employeeId <= 0) return;
    PayrollCalculator::Result result;
    if (AdvancedTab::isPayrollRulesEnabled() && !m_appliedRules.isEmpty())
        result = PayrollCalculator::calculate(m_grossPay, m_appliedRules);
    PrintHelper::printMonthlyReport(m_employeeName, m_employeeId,
                                    m_month, m_year, result, this,
                                    wagesVisible());
}

void SalaryTab::onExport() {
    if (m_employeeId <= 0) return;
    auto emp = EmployeeRepository::instance().getEmployee(m_employeeId);
    if (!emp) return;
    PayrollCalculator::Result result;
    if (AdvancedTab::isPayrollRulesEnabled() && !m_appliedRules.isEmpty())
        result = PayrollCalculator::calculate(m_grossPay, m_appliedRules);
    ExportHelper::exportMonth(m_employeeName, m_employeeId,
                              m_month, m_year, *emp, result, this,
                              wagesVisible());
}

// ── Wage visibility ───────────────────────────────────────────────────────

bool SalaryTab::wagesVisible() const {
    if (!LockPolicy::isLocked(LockPolicy::Feature::HideWages)) return true;
    if (SessionManager::isUnlocked()) return true;
    return m_selfViewActive;
}

void SalaryTab::refreshSelfViewCheckbox() {
    if (!m_selfViewCheck) return;
    const bool shouldShow = LockPolicy::isLocked(LockPolicy::Feature::HideWages)
                         && !m_adminUnlocked
                         && m_employeeId > 0
                         && [this]() {
                                auto emp = EmployeeRepository::instance().getEmployee(m_employeeId);
                                return emp && emp->hasPinSet();
                            }();
    m_selfViewCheck->setVisible(shouldShow);
    if (!shouldShow) {
        m_selfViewActive = false;
        m_selfViewTimer->stop();
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(false);
        m_selfViewCheck->blockSignals(false);
    }
}

void SalaryTab::onLockChanged(bool unlocked) {
    m_adminUnlocked = unlocked;
    refreshLockIcon();
    if (unlocked) {
        m_selfViewActive = false;
        m_selfViewTimer->stop();
        if (m_selfViewCheck) {
            m_selfViewCheck->blockSignals(true);
            m_selfViewCheck->setChecked(false);
            m_selfViewCheck->blockSignals(false);
            m_selfViewCheck->setVisible(false);
        }
    } else {
        refreshSelfViewCheckbox();
    }
    loadSummary();
}

void SalaryTab::onSelfViewToggled(bool checked) {
    if (checked) {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Employee PIN"));
        dlg.setMinimumWidth(300);
        auto* layout = new QVBoxLayout(&dlg);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 12);

        auto* prompt = new QLabel(tr("Enter your PIN to view your wages."), &dlg);
        prompt->setWordWrap(true);
        layout->addWidget(prompt);

        auto* pinEdit = new QLineEdit(&dlg);
        pinEdit->setEchoMode(QLineEdit::Password);
        pinEdit->setMaxLength(12);
        pinEdit->setPlaceholderText(tr("6–12 digits"));
        pinEdit->setValidator(new QRegularExpressionValidator(
            QRegularExpression("\\d{0,12}"), pinEdit));
        pinEdit->setLayoutDirection(Qt::LeftToRight);
        layout->addWidget(pinEdit);

        auto* errorLabel = new QLabel(&dlg);
        errorLabel->setStyleSheet(
            ThemeHelper::isDark() ? "color: #ef9a9a;" : "color: #c0392b;");
        errorLabel->hide();
        layout->addWidget(errorLabel);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        bool verified = false;
        connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
            if (EmployeePinManager::verifyPin(m_employeeId, pinEdit->text())) {
                verified = true;
                dlg.accept();
            } else {
                errorLabel->setText(tr("Incorrect PIN. Try again."));
                errorLabel->show();
                pinEdit->clear();
                pinEdit->setFocus();
                dlg.adjustSize();
            }
        });

        dlg.exec();

        if (verified) {
            m_selfViewActive = true;
            m_selfViewTimer->start();
            emit selfViewActivated(true);
        } else {
            m_selfViewCheck->blockSignals(true);
            m_selfViewCheck->setChecked(false);
            m_selfViewCheck->blockSignals(false);
            m_selfViewActive = false;
        }
    } else {
        m_selfViewActive = false;
        m_selfViewTimer->stop();
        emit selfViewActivated(false);
    }
    loadSummary();
}

void SalaryTab::onSelfViewTimeout() {
    m_selfViewActive = false;
    if (m_selfViewCheck) {
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(false);
        m_selfViewCheck->blockSignals(false);
    }
    emit selfViewActivated(false);
    loadSummary();
}

void SalaryTab::setSelfViewActive(bool active) {
    // Called by MainWindow when AttendanceTab's self-view changes
    m_selfViewActive = active;
    m_selfViewTimer->stop();
    if (m_selfViewCheck) {
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(active);
        m_selfViewCheck->blockSignals(false);
    }
    loadSummary();
}

void SalaryTab::refreshLockIcon() {
    if (!m_lockBtn) return;
    m_lockBtn->setText(m_adminUnlocked ? "🔓" : "🔒");
    m_lockBtn->setToolTip(m_adminUnlocked
        ? tr("Lock admin access")
        : tr("Unlock admin access"));
}

void SalaryTab::refreshLockBtn() {
    // Called after LockPolicyDialog closes to update visibility instantly.
    // The lock button is only meaningful when HideWages is active.
    if (!m_lockBtn) return;
    const bool show = PinManager::isPinSet()
                   && LockPolicy::isLocked(LockPolicy::Feature::HideWages);
    m_lockBtn->setVisible(show);
    refreshLockIcon();
    // Also refresh summary — HideWages may have just been toggled
    loadSummary();
}
