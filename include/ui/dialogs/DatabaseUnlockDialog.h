#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

// ── DatabaseUnlockDialog ───────────────────────────────────────────────────
//
// Shown when the DB exists but cannot be opened with the cached/fallback key.
// This happens after an OS reinstall, machine migration, or restoring a backup
// that was made with a different PIN.
//
// The admin enters their PIN, which is used to derive the SQLCipher key and
// open the DB directly. On success the derived key is stored in the OS keychain
// so subsequent launches are silent.
//
// This is NOT the session lock dialog — it appears before the main window
// opens, at the DB level. It loops on wrong PIN with no attempt limit.
//
// "Start Fresh" button provides a last-resort escape hatch for users who are
// completely locked out (forgot PIN and have no recovery file). It deletes
// the database, clears all settings, and restarts the app.

class DatabaseUnlockDialog : public QDialog {
    Q_OBJECT

public:
    explicit DatabaseUnlockDialog(const QString& dbPath,
                                   QWidget* parent = nullptr);

    // Show the dialog and attempt unlock. Returns true if DB was successfully
    // opened. Returns false if admin clicked Cancel (app should exit).
    // Loops internally on wrong PIN.
    static bool unlock(const QString& dbPath, QWidget* parent = nullptr);

private slots:
    void onUnlockClicked();
    void onStartFreshClicked();

private:
    void setupUi(const QString& dbPath);
    void showError(const QString& msg);
    void clearError();

    QLineEdit*   m_pinEdit        = nullptr;
    QPushButton* m_unlockBtn      = nullptr;
    QPushButton* m_startFreshBtn  = nullptr;
    QLabel*      m_errorLabel     = nullptr;
    QString      m_dbPath;
};
