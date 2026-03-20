#include "ui/dialogs/PayrollRuleDialog.h"
#include "utils/CurrencyManager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>

PayrollRuleDialog::PayrollRuleDialog(PayrollRule::AppliesTo defaultAppliesTo,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Payroll Rule"));
    setMinimumWidth(340);
    setMaximumWidth(480);
    setSizeGripEnabled(false);
    setupUi(defaultAppliesTo);
}

PayrollRuleDialog::PayrollRuleDialog(const PayrollRule& rule, QWidget* parent)
    : QDialog(parent), m_ruleId(rule.id), m_sortOrder(rule.sortOrder)
{
    setWindowTitle(tr("Edit Payroll Rule"));
    setMinimumWidth(340);
    setMaximumWidth(480);
    setSizeGripEnabled(false);
    setupUi(rule.appliesTo);
    populate(rule);
}

void PayrollRuleDialog::setupUi(PayrollRule::AppliesTo defaultAppliesTo) {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("e.g. Social Insurance, Transport Allowance"));
    form->addRow(tr("Name *:"), m_nameEdit);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("Deduction"),  static_cast<int>(PayrollRule::Type::Deduction));
    m_typeCombo->addItem(tr("Addition"),   static_cast<int>(PayrollRule::Type::Addition));
    form->addRow(tr("Type:"), m_typeCombo);

    m_basisCombo = new QComboBox(this);
    m_basisCombo->addItem(tr("Fixed Amount"),   static_cast<int>(PayrollRule::Basis::FixedAmount));
    m_basisCombo->addItem(tr("% of Gross Pay"), static_cast<int>(PayrollRule::Basis::PercentOfGross));
    form->addRow(tr("Basis:"), m_basisCombo);

    m_valueLabel = new QLabel(tr("Amount (%1):").arg(CurrencyManager::symbol()), this);
    m_valueSpin  = new QDoubleSpinBox(this);
    m_valueSpin->setRange(0.0, 999999.99);
    m_valueSpin->setDecimals(2);
    m_valueSpin->setSingleStep(1.0);
    form->addRow(m_valueLabel, m_valueSpin);

    m_appliesToCombo = new QComboBox(this);
    m_appliesToCombo->addItem(tr("All Employees"),     static_cast<int>(PayrollRule::AppliesTo::All));
    m_appliesToCombo->addItem(tr("Monthly Employees"), static_cast<int>(PayrollRule::AppliesTo::Monthly));
    m_appliesToCombo->addItem(tr("Hourly Employees"),  static_cast<int>(PayrollRule::AppliesTo::Hourly));
    m_appliesToCombo->setCurrentIndex(
        m_appliesToCombo->findData(static_cast<int>(defaultAppliesTo)));
    form->addRow(tr("Applies to:"), m_appliesToCombo);

    m_enabledCheck = new QCheckBox(tr("Rule is active"), this);
    m_enabledCheck->setChecked(true);
    form->addRow(QString(), m_enabledCheck);

    mainLayout->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    connect(m_basisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PayrollRuleDialog::onBasisChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &PayrollRuleDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PayrollRuleDialog::populate(const PayrollRule& r) {
    m_nameEdit->setText(r.name);
    m_typeCombo->setCurrentIndex(m_typeCombo->findData(static_cast<int>(r.type)));
    m_basisCombo->setCurrentIndex(m_basisCombo->findData(static_cast<int>(r.basis)));
    m_valueSpin->setValue(r.value);
    m_appliesToCombo->setCurrentIndex(
        m_appliesToCombo->findData(static_cast<int>(r.appliesTo)));
    m_enabledCheck->setChecked(r.enabled);
    onBasisChanged(m_basisCombo->currentIndex());
}

void PayrollRuleDialog::onBasisChanged(int) {
    const auto basis = static_cast<PayrollRule::Basis>(
        m_basisCombo->currentData().toInt());
    if (basis == PayrollRule::Basis::PercentOfGross) {
        m_valueLabel->setText(tr("Percentage (%):"));
        m_valueSpin->setRange(0.0, 100.0);
        m_valueSpin->setSuffix(" %");
        m_valueSpin->setSingleStep(0.5);
        m_valueSpin->setDecimals(2);
    } else {
        m_valueLabel->setText(tr("Amount (%1):").arg(CurrencyManager::symbol()));
        m_valueSpin->setRange(0.0, 999999.99);
        m_valueSpin->setSuffix("");
        m_valueSpin->setSingleStep(1.0);
        m_valueSpin->setDecimals(2);
    }
}

PayrollRule PayrollRuleDialog::rule() const {
    PayrollRule r;
    r.id        = m_ruleId;
    r.name      = m_nameEdit->text().trimmed();
    r.type      = static_cast<PayrollRule::Type>     (m_typeCombo->currentData().toInt());
    r.basis     = static_cast<PayrollRule::Basis>    (m_basisCombo->currentData().toInt());
    r.value     = m_valueSpin->value();
    r.appliesTo = static_cast<PayrollRule::AppliesTo>(m_appliesToCombo->currentData().toInt());
    r.enabled   = m_enabledCheck->isChecked();
    r.sortOrder = m_sortOrder;
    return r;
}

bool PayrollRuleDialog::validate() {
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Validation"), tr("Rule name is required."));
        m_nameEdit->setFocus();
        return false;
    }
    return true;
}

void PayrollRuleDialog::onAccept() {
    if (validate()) accept();
}
