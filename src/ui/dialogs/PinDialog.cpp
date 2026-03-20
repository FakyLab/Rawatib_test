#include "ui/dialogs/PinDialog.h"
#include "utils/PinManager.h"
#include "utils/ThemeHelper.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QDir>

// ── Duration formatter ────────────────────────────────────────────────────

static QString formatLockoutDuration(int seconds) {
    if (seconds >= 3600) {
        const int h = seconds / 3600;
        return PinDialog::tr("%n hour(s)", "", h);
    }
    if (seconds >= 60) {
        const int m = (seconds + 59) / 60;  // round up to whole minutes
        return PinDialog::tr("%n minute(s)", "", m);
    }
    return PinDialog::tr("%n second(s)", "", seconds);
}

// ── Constructor ───────────────────────────────────────────────────────────

PinDialog::PinDialog(const QString& title, const QString& prompt,
                     bool twoField, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumWidth(340);
    setMaximumWidth(420);
    setSizeGripEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 12);

    auto* promptLabel = new QLabel(prompt, this);
    promptLabel->setWordWrap(true);
    layout->addWidget(promptLabel);

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_pin1Edit = new QLineEdit(this);
    m_pin1Edit->setEchoMode(QLineEdit::Password);
    m_pin1Edit->setMaxLength(20);
    m_pin1Edit->setPlaceholderText(tr("6–20 characters"));
    m_pin1Edit->setLayoutDirection(Qt::LeftToRight);
    form->addRow(twoField ? tr("New password:") : tr("Password:"), m_pin1Edit);

    if (twoField) {
        m_pin2Edit = new QLineEdit(this);
        m_pin2Edit->setEchoMode(QLineEdit::Password);
        m_pin2Edit->setMaxLength(20);
        m_pin2Edit->setPlaceholderText(tr("6–20 characters"));
        m_pin2Edit->setLayoutDirection(Qt::LeftToRight);
        form->addRow(tr("Confirm:"), m_pin2Edit);
    }

    layout->addLayout(form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(
        ThemeHelper::isDark() ? "color: #ef9a9a;" : "color: #E53935;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_pin1Edit->setFocus();
}

// ── Accessors ─────────────────────────────────────────────────────────────

QString PinDialog::pin1() const {
    return m_pin1Edit ? m_pin1Edit->text() : QString();
}

QString PinDialog::pin2() const {
    return m_pin2Edit ? m_pin2Edit->text() : QString();
}

void PinDialog::showError(const QString& msg) {
    m_errorLabel->setText(msg);
    m_errorLabel->show();
    m_pin1Edit->clear();
    if (m_pin2Edit) m_pin2Edit->clear();
    m_pin1Edit->setFocus();
    adjustSize();
}

// ── Static helpers ────────────────────────────────────────────────────────

bool PinDialog::requestUnlock(QWidget* parent) {
    PinDialog dlg(
        PinDialog::tr("Admin Unlock"),
        PinDialog::tr("Enter the admin password to unlock:"),
        false, parent
    );

    // Show lockout message immediately if already locked
    const int initialLock = PinManager::getLockoutSeconds();
    if (initialLock > 0)
        dlg.showError(PinDialog::tr("Too many failed attempts. Try again in %1.")
                          .arg(formatLockoutDuration(initialLock)));

    while (dlg.exec() == QDialog::Accepted) {
        const int remaining = PinManager::getLockoutSeconds();
        if (remaining > 0) {
            dlg.showError(PinDialog::tr("Too many failed attempts. Try again in %1.")
                              .arg(formatLockoutDuration(remaining)));
            continue;
        }
        if (PinManager::verifyPin(dlg.pin1()))
            return true;
        // Show attempt count warning when approaching lockout
        const int attempts = PinManager::getFailedAttempts();
        if (attempts >= 3 && attempts < 5) {
            dlg.showError(PinDialog::tr("Incorrect password. %n attempt(s) remaining before lockout.", "", 5 - attempts));
        } else if (attempts >= 5) {
            const int secs = PinManager::getLockoutSeconds();
            dlg.showError(PinDialog::tr("Too many failed attempts. Try again in %1.")
                              .arg(formatLockoutDuration(secs)));
        } else {
            dlg.showError(PinDialog::tr("Incorrect password. Try again."));
        }
    }
    return false;
}

bool PinDialog::requestUnlock(QWidget* parent, const QString& prompt) {
    PinDialog dlg(
        PinDialog::tr("Confirm Password"),
        prompt,
        false, parent
    );

    const int initialLock = PinManager::getLockoutSeconds();
    if (initialLock > 0)
        dlg.showError(PinDialog::tr("Too many failed attempts. Try again in %1.")
                          .arg(formatLockoutDuration(initialLock)));

    while (dlg.exec() == QDialog::Accepted) {
        const int remaining = PinManager::getLockoutSeconds();
        if (remaining > 0) {
            dlg.showError(PinDialog::tr("Too many failed attempts. Try again in %1.")
                              .arg(formatLockoutDuration(remaining)));
            continue;
        }
        if (PinManager::verifyPin(dlg.pin1()))
            return true;
        const int attempts = PinManager::getFailedAttempts();
        if (attempts >= 3 && attempts < 5) {
            dlg.showError(PinDialog::tr("Incorrect password. %n attempt(s) remaining before lockout.", "", 5 - attempts));
        } else if (attempts >= 5) {
            const int secs = PinManager::getLockoutSeconds();
            dlg.showError(PinDialog::tr("Too many failed attempts. Try again in %1.")
                              .arg(formatLockoutDuration(secs)));
        } else {
            dlg.showError(PinDialog::tr("Incorrect password. Try again."));
        }
    }
    return false;
}

bool PinDialog::showSetPin(QWidget* parent) {
    PinDialog dlg(
        PinDialog::tr("Set Admin Password"),
        PinDialog::tr("Choose a password to protect admin operations.\n"
                      "Must be 6–20 characters."),
        true, parent
    );
    while (dlg.exec() == QDialog::Accepted) {
        const QString p1 = dlg.pin1();
        const QString p2 = dlg.pin2();
        if (!PinManager::isValidPassword(p1)) {
            dlg.showError(PinDialog::tr("Password must be 6–20 characters."));
            continue;
        }
        if (p1 != p2) {
            dlg.showError(PinDialog::tr("Passwords do not match. Try again."));
            continue;
        }
        if (PinManager::setPin(p1)) {
            // Offer to save the recovery file immediately after first PIN set.
            // The recovery file is needed for --bypass-key emergency access.
            const QByteArray fileData = PinManager::generateRecoveryFileData();
            if (!fileData.isEmpty()) {
                const QString savePath = QFileDialog::getSaveFileName(
                    parent,
                    PinDialog::tr("Save Recovery File"),
                    QDir::homePath() + "/" + PinManager::recoveryFileName(),
                    PinDialog::tr("Recovery Files (*.rwtrec);;All Files (*)"));

                if (!savePath.isEmpty()) {
                    QFile f(savePath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(fileData);
                        f.close();
                        QMessageBox::information(parent,
                            PinDialog::tr("Recovery File Saved"),
                            PinDialog::tr(
                                "Recovery file saved successfully.\n\n"
                                "Keep it in a safe place (USB drive, secure cloud storage).\n"
                                "You will need it to reset your password if you forget it.\n\n"
                                "This file remains valid even if you change your password later."));
                    } else {
                        QMessageBox::warning(parent,
                            PinDialog::tr("Save Failed"),
                            PinDialog::tr(
                                "Could not write the recovery file.\n"
                                "You can generate it later from Settings \u2192 Security."));
                    }
                } else {
                    // Admin dismissed the save dialog
                    QMessageBox::warning(parent,
                        PinDialog::tr("Recovery File Not Saved"),
                        PinDialog::tr(
                            "The recovery file was not saved.\n\n"
                            "Without it, you cannot recover access "
                            "if you forget your password.\n\n"
                            "You can generate it later from Settings \u2192 Security."));
                }
            }
            return true;
        }
    }
    return false;
}

bool PinDialog::showChangePin(QWidget* parent) {
    // Step 1: verify current password
    QString currentPassword;
    {
        PinDialog verify(
            PinDialog::tr("Change Admin Password"),
            PinDialog::tr("Enter your current password:"),
            false, parent
        );
        while (verify.exec() == QDialog::Accepted) {
            if (PinManager::verifyPin(verify.pin1())) {
                currentPassword = verify.pin1();
                break;
            }
            verify.showError(PinDialog::tr("Incorrect password. Try again."));
        }
        if (verify.result() != QDialog::Accepted)
            return false;
    }

    // Step 2: enter new password
    PinDialog newPwd(
        PinDialog::tr("Change Admin Password"),
        PinDialog::tr("Enter your new password.\nMust be 6–20 characters."),
        true, parent
    );
    while (newPwd.exec() == QDialog::Accepted) {
        const QString p1 = newPwd.pin1();
        const QString p2 = newPwd.pin2();
        if (!PinManager::isValidPassword(p1)) {
            newPwd.showError(PinDialog::tr("Password must be 6–20 characters."));
            continue;
        }
        if (p1 != p2) {
            newPwd.showError(PinDialog::tr("Passwords do not match. Try again."));
            continue;
        }
        if (PinManager::changePin(currentPassword, p1))
            return true;
    }
    return false;
}

bool PinDialog::showRemovePin(QWidget* parent) {
    PinDialog dlg(
        PinDialog::tr("Remove Admin Password"),
        PinDialog::tr("Enter your current password to remove admin protection:"),
        false, parent
    );
    while (dlg.exec() == QDialog::Accepted) {
        if (PinManager::removePin(dlg.pin1()))
            return true;
        dlg.showError(PinDialog::tr("Incorrect password. Try again."));
    }
    return false;
}
