#include "ui/dialogs/LicenseDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QFile>
#include <QFont>

LicenseDialog::LicenseDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("License"));
    setMinimumSize(640, 480);
    resize(700, 520);
    setSizeGripEnabled(true);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 10);

    // ── Tabs ──────────────────────────────────────────────────────────────
    auto* tabs = new QTabWidget(this);

    auto makeTextEdit = [](const QString& resourcePath) -> QTextEdit* {
        auto* edit = new QTextEdit();
        edit->setReadOnly(true);
        QFont mono("Courier New", 9);
        mono.setStyleHint(QFont::Monospace);
        edit->setFont(mono);
        edit->setLineWrapMode(QTextEdit::NoWrap);

        QFile f(resourcePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            edit->setPlainText(QString::fromUtf8(f.readAll()));
        else
            edit->setPlainText(QString("License file not found: %1").arg(resourcePath));

        return edit;
    };

    // Tab 1 — Rawatib license
    tabs->addTab(makeTextEdit(":/resources/LICENSE.txt"),
                 tr("Rawatib"));

    // Tab 2 — Third-party licenses
    tabs->addTab(makeTextEdit(":/resources/THIRD_PARTY_LICENSES.txt"),
                 tr("Third-Party Libraries"));

    mainLayout->addWidget(tabs, 1);

    // ── Close button ──────────────────────────────────────────────────────
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);
}
