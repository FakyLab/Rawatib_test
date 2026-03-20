#include "ui/dialogs/EmployeeDialog.h"
#include "utils/CurrencyManager.h"
#include "utils/EmployeePinManager.h"
#include "utils/ThemeHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QRegularExpressionValidator>

EmployeeDialog::EmployeeDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Add Employee"));
    setMinimumWidth(360);
    setMaximumWidth(520);
    setSizeGripEnabled(false);
    setupUi();
}

EmployeeDialog::EmployeeDialog(const Employee& employee, QWidget* parent)
    : QDialog(parent), m_employeeId(employee.id),
      m_hasPinSet(employee.hasPinSet()) {
    setWindowTitle(tr("Edit Employee"));
    setMinimumWidth(360);
    setMaximumWidth(520);
    setSizeGripEnabled(false);
    setupUi();
    populate(employee);
}

void EmployeeDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // ── Basic fields ──────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Required"));
    m_phoneEdit = new QLineEdit(this);
    m_phoneEdit->setPlaceholderText(tr("Optional"));
    form->addRow(tr("Full Name *:"), m_nameEdit);
    form->addRow(tr("Phone:"),       m_phoneEdit);
    mainLayout->addLayout(form);

    // ── Pay type selector ─────────────────────────────────────────────────
    auto* payGroup = new QGroupBox(tr("Pay Type"), this);
    auto* payRow   = new QHBoxLayout(payGroup);
    m_hourlyRadio  = new QRadioButton(tr("Hourly"),         payGroup);
    m_monthlyRadio = new QRadioButton(tr("Monthly Salary"), payGroup);
    m_hourlyRadio->setChecked(true);
    payRow->addWidget(m_hourlyRadio);
    payRow->addWidget(m_monthlyRadio);
    payRow->addStretch();
    mainLayout->addWidget(payGroup);

    // ── Hourly section ────────────────────────────────────────────────────
    m_hourlyWidget = new QWidget(this);
    auto* hourlyForm = new QFormLayout(m_hourlyWidget);
    hourlyForm->setContentsMargins(0, 0, 0, 0);

    m_wageSpin = new QDoubleSpinBox(m_hourlyWidget);
    m_wageSpin->setRange(0.0, 99999.99);
    m_wageSpin->setDecimals(2);
    m_wageSpin->setSuffix(" " + CurrencyManager::symbol() + tr("/hr"));

    m_wageWarning = new QLabel(m_hourlyWidget);
    m_wageWarning->setText(tr("⚠  Hourly wage is 0 — salary calculations will show zero."));
    m_wageWarning->setWordWrap(true);
    m_wageWarning->setStyleSheet(
        ThemeHelper::isDark()
        ? "QLabel { color: #ffcc80; background: #3d2800; "
          "border: 1px solid #8a6200; border-radius: 3px; padding: 4px 6px; font-size: 9pt; }"
        : "QLabel { color: #7a4f00; background: #fff8e1; "
          "border: 1px solid #f0c040; border-radius: 3px; padding: 4px 6px; font-size: 9pt; }");
    m_wageWarning->setVisible(false);

    hourlyForm->addRow(tr("Hourly Wage *:"), m_wageSpin);
    hourlyForm->addRow(QString(),            m_wageWarning);
    mainLayout->addWidget(m_hourlyWidget);

    // ── Monthly salary section ────────────────────────────────────────────
    m_monthlyWidget = new QWidget(this);
    auto* monthlyForm = new QFormLayout(m_monthlyWidget);
    monthlyForm->setContentsMargins(0, 0, 0, 0);

    m_salarySpin = new QDoubleSpinBox(m_monthlyWidget);
    m_salarySpin->setRange(0.0, 9999999.99);
    m_salarySpin->setDecimals(2);
    m_salarySpin->setSuffix(" " + CurrencyManager::symbol() + tr("/month"));

    m_workDaysSpin = new QSpinBox(m_monthlyWidget);
    m_workDaysSpin->setRange(1, 31);
    m_workDaysSpin->setValue(26);
    m_workDaysSpin->setToolTip(
        tr("Number of working days used to divide the monthly salary into a daily rate."));

    m_expectedCiEdit = new QTimeEdit(m_monthlyWidget);
    m_expectedCiEdit->setDisplayFormat("hh:mm AP");
    m_expectedCiEdit->setTime(QTime(9, 0));

    m_expectedCoEdit = new QTimeEdit(m_monthlyWidget);
    m_expectedCoEdit->setDisplayFormat("hh:mm AP");
    m_expectedCoEdit->setTime(QTime(17, 0));

    m_lateTolSpin = new QSpinBox(m_monthlyWidget);
    m_lateTolSpin->setRange(0, 60);
    m_lateTolSpin->setValue(15);
    m_lateTolSpin->setSuffix(tr(" min"));
    m_lateTolSpin->setToolTip(
        tr("Arrival within this many minutes after expected check-in is not deducted."));

    m_dailyRateLabel = new QLabel(m_monthlyWidget);
    m_dailyRateLabel->setStyleSheet("color: palette(mid); font-size: 9pt;");

    m_salaryWarning = new QLabel(m_monthlyWidget);
    m_salaryWarning->setText(
        tr("⚠  Monthly salary is 0 — salary calculations will show zero."));
    m_salaryWarning->setWordWrap(true);
    m_salaryWarning->setStyleSheet(
        ThemeHelper::isDark()
        ? "QLabel { color: #ffcc80; background: #3d2800; "
          "border: 1px solid #8a6200; border-radius: 3px; padding: 4px 6px; font-size: 9pt; }"
        : "QLabel { color: #7a4f00; background: #fff8e1; "
          "border: 1px solid #f0c040; border-radius: 3px; padding: 4px 6px; font-size: 9pt; }");
    m_salaryWarning->setVisible(false);

    monthlyForm->addRow(tr("Monthly Salary *:"),   m_salarySpin);
    monthlyForm->addRow(QString(),                 m_salaryWarning);
    monthlyForm->addRow(tr("Working Days/Month:"), m_workDaysSpin);
    monthlyForm->addRow(QString(),                 m_dailyRateLabel);

    // "Use expected times" checkbox — when unchecked, time fields are
    // disabled and QTime() (invalid) is saved, disabling time-based deductions.
    m_expectedTimesCheck = new QCheckBox(
        tr("Use expected check-in / check-out times"), m_monthlyWidget);
    m_expectedTimesCheck->setChecked(true);
    m_expectedTimesCheck->setToolTip(
        tr("When enabled, late arrival and early departure deductions are\n"
           "calculated based on the expected times set below.\n"
           "Disable for employees with flexible schedules."));
    monthlyForm->addRow(QString(), m_expectedTimesCheck);

    monthlyForm->addRow(tr("Expected Check-In:"),  m_expectedCiEdit);
    monthlyForm->addRow(tr("Expected Check-Out:"), m_expectedCoEdit);
    monthlyForm->addRow(tr("Late Tolerance:"),     m_lateTolSpin);

    connect(m_expectedTimesCheck, &QCheckBox::toggled,
            this, &EmployeeDialog::onExpectedTimesToggled);

    m_monthlyWidget->setVisible(false);
    mainLayout->addWidget(m_monthlyWidget);

    // ── Notes ─────────────────────────────────────────────────────────────
    auto* notesForm = new QFormLayout();
    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setFixedHeight(m_notesEdit->fontMetrics().lineSpacing() * 3 + 12);
    m_notesEdit->setPlaceholderText(tr("Optional"));
    notesForm->addRow(tr("Notes:"), m_notesEdit);
    mainLayout->addLayout(notesForm);

    // ── PIN section ───────────────────────────────────────────────────────
    // The PIN button text switches between "Set" and "Change" depending on
    // whether the employee already has a PIN. The Clear button only appears
    // when editing an employee that already has a PIN.
    auto* pinGroup  = new QGroupBox(this);
    pinGroup->setFlat(true);
    auto* pinLayout = new QVBoxLayout(pinGroup);
    pinLayout->setSpacing(6);
    pinLayout->setContentsMargins(0, 0, 0, 0);

    // Description label
    auto* pinDesc = new QLabel(
        tr("Allows this employee to mark their own records as paid without admin unlock "
           "(when enabled in Lock Policy)."), pinGroup);
    pinDesc->setWordWrap(true);
    pinDesc->setStyleSheet("color: palette(mid); font-size: 9pt;");
    pinLayout->addWidget(pinDesc);

    // Button row: [Set/Change PIN]  [Clear PIN]
    auto* pinBtnRow = new QHBoxLayout();
    m_setPinBtn = new QPushButton(
        m_hasPinSet ? tr("Change employee PIN") : tr("Set employee PIN"), pinGroup);
    m_clearPinBtn = new QPushButton(tr("Clear PIN"), pinGroup);
    m_clearPinBtn->setVisible(m_hasPinSet);
    pinBtnRow->addWidget(m_setPinBtn);
    pinBtnRow->addWidget(m_clearPinBtn);
    pinBtnRow->addStretch();
    pinLayout->addLayout(pinBtnRow);

    // PIN input fields — hidden until user clicks Set/Change
    m_pinFields = new QWidget(pinGroup);
    auto* pinFieldForm = new QFormLayout(m_pinFields);
    pinFieldForm->setContentsMargins(0, 0, 0, 0);

    m_pinEdit = new QLineEdit(m_pinFields);
    m_pinEdit->setEchoMode(QLineEdit::Password);
    m_pinEdit->setMaxLength(12);
    m_pinEdit->setPlaceholderText(tr("6-12 digits"));
    m_pinEdit->setLayoutDirection(Qt::LeftToRight);
    m_pinEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression("\\d{0,12}"), m_pinEdit));

    m_pinConfirm = new QLineEdit(m_pinFields);
    m_pinConfirm->setEchoMode(QLineEdit::Password);
    m_pinConfirm->setMaxLength(12);
    m_pinConfirm->setPlaceholderText(tr("Repeat PIN"));
    m_pinConfirm->setLayoutDirection(Qt::LeftToRight);
    m_pinConfirm->setValidator(new QRegularExpressionValidator(
        QRegularExpression("\\d{0,12}"), m_pinConfirm));

    m_pinError = new QLabel(m_pinFields);
    m_pinError->setStyleSheet(
        ThemeHelper::isDark()
        ? "color: #ef9a9a; font-size: 9pt;"
        : "color: #E53935; font-size: 9pt;");
    m_pinError->setVisible(false);

    pinFieldForm->addRow(tr("New PIN:"),     m_pinEdit);
    pinFieldForm->addRow(tr("Confirm PIN:"), m_pinConfirm);
    pinFieldForm->addRow(QString(),          m_pinError);
    m_pinFields->setVisible(false);
    pinLayout->addWidget(m_pinFields);

    mainLayout->addWidget(pinGroup);

    // ── OK / Cancel ───────────────────────────────────────────────────────
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_hourlyRadio,  &QRadioButton::toggled,
            this, &EmployeeDialog::onPayTypeChanged);
    connect(m_wageSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &EmployeeDialog::onWageChanged);
    connect(m_salarySpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &EmployeeDialog::onSalaryChanged);
    connect(m_workDaysSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { onSalaryChanged(m_salarySpin->value()); });
    connect(m_setPinBtn,   &QPushButton::clicked, this, &EmployeeDialog::onSetPinClicked);
    connect(m_clearPinBtn, &QPushButton::clicked, this, &EmployeeDialog::onClearPinClicked);
    connect(buttons, &QDialogButtonBox::accepted, this, &EmployeeDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ── Slots ─────────────────────────────────────────────────────────────────

void EmployeeDialog::onPayTypeChanged() {
    const bool hourly = m_hourlyRadio->isChecked();
    m_hourlyWidget->setVisible(hourly);
    m_monthlyWidget->setVisible(!hourly);
    adjustSize();
}

void EmployeeDialog::onWageChanged(double value) {
    m_wageWarning->setVisible(value == 0.0);
    adjustSize();
}

void EmployeeDialog::onSalaryChanged(double value) {
    m_salaryWarning->setVisible(value == 0.0);
    const int days = m_workDaysSpin->value();
    if (days > 0 && value > 0.0)
        m_dailyRateLabel->setText(
            tr("Daily rate: %1").arg(CurrencyManager::format(value / days)));
    else
        m_dailyRateLabel->setText(QString());
    adjustSize();
}

void EmployeeDialog::onExpectedTimesToggled(bool checked) {
    m_expectedCiEdit->setEnabled(checked);
    m_expectedCoEdit->setEnabled(checked);
    m_lateTolSpin->setEnabled(checked);
    adjustSize();
}

void EmployeeDialog::onSetPinClicked() {
    // Toggle PIN input fields visibility
    const bool show = !m_pinFields->isVisible();
    m_pinFields->setVisible(show);
    if (show) {
        m_pinEdit->clear();
        m_pinConfirm->clear();
        m_pinError->setVisible(false);
        m_pinEdit->setFocus();
    }
    adjustSize();
}

void EmployeeDialog::onClearPinClicked() {
    auto reply = QMessageBox::question(this,
        tr("Clear Employee PIN"),
        tr("Remove this employee's PIN?\n\n"
           "They will no longer be able to mark their own records as paid."),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        m_pinAction = PinAction::Clear;
        m_pinFields->setVisible(false);
        m_clearPinBtn->setVisible(false);
        m_setPinBtn->setText(tr("Set employee PIN"));
        adjustSize();
    }
}

// ── populate ─────────────────────────────────────────────────────────────

void EmployeeDialog::populate(const Employee& e) {
    m_nameEdit->setText(e.name);
    m_phoneEdit->setText(e.phone);
    m_notesEdit->setPlainText(e.notes);

    if (e.isMonthly()) {
        m_monthlyRadio->setChecked(true);
        m_salarySpin->setValue(e.monthlySalary);
        m_workDaysSpin->setValue(e.workingDaysPerMonth);

        // Set checkbox based on whether expected times are stored
        const bool hasExpectedTimes = e.expectedCheckin.isValid();
        m_expectedTimesCheck->setChecked(hasExpectedTimes);
        m_expectedCiEdit->setEnabled(hasExpectedTimes);
        m_expectedCoEdit->setEnabled(hasExpectedTimes);
        m_lateTolSpin->setEnabled(hasExpectedTimes);

        if (e.expectedCheckin.isValid())
            m_expectedCiEdit->setTime(e.expectedCheckin);
        if (e.expectedCheckout.isValid())
            m_expectedCoEdit->setTime(e.expectedCheckout);
        m_lateTolSpin->setValue(e.lateToleranceMin);
        onSalaryChanged(e.monthlySalary);
    } else {
        m_hourlyRadio->setChecked(true);
        m_wageSpin->setValue(e.hourlyWage);
        onWageChanged(e.hourlyWage);
    }
    onPayTypeChanged();
}

// ── employee() ────────────────────────────────────────────────────────────

Employee EmployeeDialog::employee() const {
    Employee e;
    e.id    = m_employeeId;
    e.name  = m_nameEdit->text().trimmed();
    e.phone = m_phoneEdit->text().trimmed();
    e.notes = m_notesEdit->toPlainText().trimmed();

    if (m_monthlyRadio->isChecked()) {
        e.payType             = PayType::Monthly;
        e.monthlySalary       = m_salarySpin->value();
        e.workingDaysPerMonth = m_workDaysSpin->value();
        // Only save expected times if checkbox is checked
        if (m_expectedTimesCheck->isChecked()) {
            e.expectedCheckin  = m_expectedCiEdit->time();
            e.expectedCheckout = m_expectedCoEdit->time();
            e.lateToleranceMin = m_lateTolSpin->value();
        } else {
            e.expectedCheckin  = QTime();   // invalid = not set
            e.expectedCheckout = QTime();
            e.lateToleranceMin = m_lateTolSpin->value();
        }
    } else {
        e.payType    = PayType::Hourly;
        e.hourlyWage = m_wageSpin->value();
    }
    return e;
}

// ── validate ─────────────────────────────────────────────────────────────

bool EmployeeDialog::validate() {
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Validation"),
            tr("Employee name is required."));
        m_nameEdit->setFocus();
        return false;
    }
    if (m_monthlyRadio->isChecked()) {
        if (m_expectedCiEdit->time().isValid() &&
            m_expectedCoEdit->time().isValid() &&
            m_expectedCoEdit->time() <= m_expectedCiEdit->time()) {
            QMessageBox::warning(this, tr("Validation"),
                tr("Expected check-out must be after expected check-in."));
            m_expectedCoEdit->setFocus();
            return false;
        }
    }
    // Validate PIN if fields are visible
    if (m_pinFields->isVisible()) {
        const QString pin  = m_pinEdit->text();
        const QString conf = m_pinConfirm->text();
        if (!EmployeePinManager::isValidPin(pin)) {
            m_pinError->setText(
                tr("PIN must be 6-12 digits and cannot be a simple sequence "
                   "(e.g. 111111, 123456)."));
            m_pinError->setVisible(true);
            m_pinEdit->setFocus();
            adjustSize();
            return false;
        }
        if (pin != conf) {
            m_pinError->setText(tr("PINs do not match."));
            m_pinError->setVisible(true);
            m_pinConfirm->setFocus();
            adjustSize();
            return false;
        }
        // PIN is valid — record it
        m_pinAction = PinAction::SetNew;
        m_newPin    = pin;
    }
    return true;
}

void EmployeeDialog::onAccept() {
    if (validate()) accept();
}
