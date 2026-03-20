#pragma once
#include <QDialog>
#include "ui/AdvancedTab.h"

// ── PayrollRulesDialog ─────────────────────────────────────────────────────
//
// Thin wrapper that hosts AdvancedTab inside a QDialog.
// Opened from Advanced → Payroll Rules... in the menu bar.
// Forwards AdvancedTab::rulesChanged so MainWindow can connect it
// to SalaryTab::onRulesChanged.

class PayrollRulesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PayrollRulesDialog(QWidget* parent = nullptr);

signals:
    void rulesChanged();

private:
    AdvancedTab* m_tab = nullptr;
};
