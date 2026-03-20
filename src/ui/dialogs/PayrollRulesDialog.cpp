#include "ui/dialogs/PayrollRulesDialog.h"
#include <QVBoxLayout>
#include <QDialogButtonBox>

PayrollRulesDialog::PayrollRulesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Payroll Rules"));
    setMinimumSize(520, 400);
    setSizeGripEnabled(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(0);

    m_tab = new AdvancedTab(this);
    layout->addWidget(m_tab);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    layout->setContentsMargins(8, 8, 8, 8);

    // Forward rulesChanged so the caller (MainWindow) can connect it
    connect(m_tab, &AdvancedTab::rulesChanged, this, &PayrollRulesDialog::rulesChanged);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
}
