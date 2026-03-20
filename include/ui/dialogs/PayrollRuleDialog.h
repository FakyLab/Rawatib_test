#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include "models/PayrollRule.h"

// ── PayrollRuleDialog ──────────────────────────────────────────────────────
//
// Add/edit a single PayrollRule.
// Fields: Name, Type, Basis, Value, Applies To, Active.
// When opened from a specific tab, defaultAppliesTo pre-sets the combo.

class PayrollRuleDialog : public QDialog {
    Q_OBJECT

public:
    // Add mode — pre-sets Applies To based on which tab opened the dialog
    explicit PayrollRuleDialog(
        PayrollRule::AppliesTo defaultAppliesTo = PayrollRule::AppliesTo::All,
        QWidget* parent = nullptr);
    // Edit mode
    explicit PayrollRuleDialog(const PayrollRule& rule, QWidget* parent = nullptr);

    PayrollRule rule() const;

private slots:
    void onBasisChanged(int index);
    void onAccept();

private:
    void setupUi(PayrollRule::AppliesTo defaultAppliesTo);
    void populate(const PayrollRule& rule);
    bool validate();

    QLineEdit*      m_nameEdit      = nullptr;
    QComboBox*      m_typeCombo     = nullptr;
    QComboBox*      m_basisCombo    = nullptr;
    QDoubleSpinBox* m_valueSpin     = nullptr;
    QLabel*         m_valueLabel    = nullptr;
    QComboBox*      m_appliesToCombo = nullptr;
    QCheckBox*      m_enabledCheck  = nullptr;

    int m_ruleId    = 0;
    int m_sortOrder = 0;
};
