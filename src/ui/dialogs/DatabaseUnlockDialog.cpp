#include "ui/dialogs/DatabaseUnlockDialog.h"
#include "database/DatabaseManager.h"
#include "utils/SettingsManager.h"
#include "utils/PinManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFrame>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QSqlDatabase>

static inline QString tr(const char* key) {
    return QCoreApplication::translate("DatabaseUnlockDialog", key);
}

DatabaseUnlockDialog::DatabaseUnlockDialog(const QString& dbPath,
                                             QWidget* parent)
    : QDialog(parent), m_dbPath(dbPath)
{
    setWindowTitle(tr("Database Locked"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(400);
    setMaximumWidth(520);
    setSizeGripEnabled(false);
    setupUi(dbPath);
}

void DatabaseUnlockDialog::setupUi(const QString& dbPath)
{
    Q_UNUSED(dbPath)
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 20, 20, 16);

    // Icon + heading
    auto* headingRow = new QHBoxLayout();
    auto* iconLabel  = new QLabel("\xf0\x9f\x94\x92", this);   // 🔒 UTF-8
    iconLabel->setStyleSheet("font-size: 28px;");
    auto* titleLabel = new QLabel(tr("Database Locked"), this);
    titleLabel->setStyleSheet("font-size: 15px; font-weight: bold;");
    headingRow->addWidget(iconLabel);
    headingRow->addWidget(titleLabel);
    headingRow->addStretch();
    layout->addLayout(headingRow);

    // Explanation
    auto* infoLabel = new QLabel(
        tr("This database was encrypted with a password.\n"
           "Enter your password to open it."), this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: palette(mid);");
    layout->addWidget(infoLabel);

    // Password field
    auto* form = new QFormLayout();
    m_pinEdit = new QLineEdit(this);
    m_pinEdit->setEchoMode(QLineEdit::Password);
    m_pinEdit->setPlaceholderText(tr("Password"));
    m_pinEdit->setMinimumWidth(220);
    form->addRow(tr("Password:"), m_pinEdit);
    layout->addLayout(form);

    // Inline error label (hidden until needed)
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #E53935; font-size: 12px;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    // Buttons
    auto* btnRow = new QHBoxLayout();
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    m_unlockBtn     = new QPushButton(tr("Open Database"), this);
    m_unlockBtn->setDefault(true);
    m_unlockBtn->setStyleSheet(
        "QPushButton { background: #1565C0; color: white; "
        "padding: 6px 18px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:disabled { background: #90A4AE; }");

    connect(cancelBtn,    &QPushButton::clicked, this, &QDialog::reject);
    connect(m_unlockBtn,  &QPushButton::clicked, this, &DatabaseUnlockDialog::onUnlockClicked);
    connect(m_pinEdit,    &QLineEdit::returnPressed, this, &DatabaseUnlockDialog::onUnlockClicked);

    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_unlockBtn);
    layout->addLayout(btnRow);

    // ── Start Fresh section — visually separated ──────────────────────────
    // Last-resort escape hatch for users who are completely locked out:
    // forgot PIN and have no recovery file. Deletes the DB and restarts.
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setStyleSheet("margin-top: 4px;");
    layout->addWidget(separator);

    auto* lockedOutLabel = new QLabel(
        tr("Locked out completely? No password and no recovery file?"), this);
    lockedOutLabel->setWordWrap(true);
    lockedOutLabel->setStyleSheet("color: palette(mid); font-size: 11px;");
    layout->addWidget(lockedOutLabel);

    m_startFreshBtn = new QPushButton(
        tr("⚠  Start Fresh — Delete All Data"), this);
    m_startFreshBtn->setStyleSheet(
        "QPushButton { color: #B71C1C; border: 1px solid #EF9A9A; "
        "padding: 5px 12px; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background: #FFEBEE; }");
    m_startFreshBtn->setToolTip(
        tr("Permanently deletes the database and all settings.\n"
           "Use only as a last resort. This cannot be undone."));

    auto* freshRow = new QHBoxLayout();
    freshRow->addStretch();
    freshRow->addWidget(m_startFreshBtn);
    layout->addLayout(freshRow);

    connect(m_startFreshBtn, &QPushButton::clicked,
            this, &DatabaseUnlockDialog::onStartFreshClicked);
}

void DatabaseUnlockDialog::onStartFreshClicked() {
    const auto confirm = QMessageBox::warning(this,
        tr("Delete All Data — Are You Sure?"),
        tr("This will permanently delete your database and all saved settings.\n\n"
           "All employees, attendance records, payroll rules, and audit log "
           "will be lost forever.\n\n"
           "This cannot be undone. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (confirm != QMessageBox::Yes) return;

    // Close DB connection if somehow open — safety measure
    auto& dbManager = DatabaseManager::instance();
    if (dbManager.isOpen()) {
        dbManager.database().close();
        QSqlDatabase::removeDatabase("main_connection");
    }

    // Delete keychain entry, DB file and its lock file
    PinManager::deleteKey();
    QFile::remove(m_dbPath);
    QFile::remove(m_dbPath + ".lock");

    // Clear QSettings
    SettingsManager::resetAll();

    // Restart fresh
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});

    // Close this dialog — main.cpp will exit cleanly
    reject();
}

void DatabaseUnlockDialog::showError(const QString& msg) {
    m_errorLabel->setText(msg);
    m_errorLabel->show();
    m_pinEdit->clear();
    m_pinEdit->setFocus();
    adjustSize();
}

void DatabaseUnlockDialog::clearError() {
    m_errorLabel->hide();
    m_errorLabel->clear();
}

void DatabaseUnlockDialog::onUnlockClicked() {
    const QString pin = m_pinEdit->text();
    if (pin.isEmpty()) {
        showError(tr("Please enter your password."));
        return;
    }

    clearError();
    m_unlockBtn->setEnabled(false);
    m_unlockBtn->setText(tr("Opening..."));
    QCoreApplication::processEvents();   // let the UI update before blocking PBKDF2

    const bool ok = DatabaseManager::instance().tryUnlockWithPin(pin);

    m_unlockBtn->setEnabled(true);
    m_unlockBtn->setText(tr("Open Database"));

    if (ok) {
        accept();
    } else {
        showError(tr("Incorrect password. Please try again."));
    }
}

// ── Static convenience ─────────────────────────────────────────────────────

bool DatabaseUnlockDialog::unlock(const QString& dbPath, QWidget* parent) {
    DatabaseUnlockDialog dlg(dbPath, parent);
    return dlg.exec() == QDialog::Accepted;
}
