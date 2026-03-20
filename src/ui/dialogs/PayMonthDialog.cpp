#include "ui/dialogs/PayMonthDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QGuiApplication>
#include <QFrame>

static inline QLocale appLocale() {
    return QGuiApplication::layoutDirection() == Qt::RightToLeft
               ? QLocale(QLocale::Arabic) : QLocale::c();
}

PayMonthDialog::PayMonthDialog(const QString& employeeName,
                               int month, int year,
                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Pay Entire Month"));
    setMinimumWidth(340);
    setMaximumWidth(480);
    setSizeGripEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 12);

    // Confirmation message
    auto* msgLabel = new QLabel(
        tr("Mark all unpaid records in <b>%1 %2</b> as paid for <b>%3</b>?")
            .arg(appLocale().monthName(month))
            .arg(year)
            .arg(employeeName),
        this);
    msgLabel->setWordWrap(true);
    layout->addWidget(msgLabel);

    // Separator
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Print checkbox — unchecked by default
    m_printCheck = new QCheckBox(tr("Print payment slip"), this);
    m_printCheck->setChecked(false);
    layout->addWidget(m_printCheck);

    // QDialogButtonBox handles platform-native button order automatically:
    // Yes/No on Windows, No/Yes (Cancel/Confirm) on macOS.
    auto* buttons = new QDialogButtonBox(this);
    auto* yesBtn  = buttons->addButton(tr("Yes"), QDialogButtonBox::AcceptRole);
    auto* noBtn   = buttons->addButton(tr("No"),  QDialogButtonBox::RejectRole);
    Q_UNUSED(noBtn)
    yesBtn->setDefault(true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool PayMonthDialog::printRequested() const {
    return m_printCheck->isChecked();
}
