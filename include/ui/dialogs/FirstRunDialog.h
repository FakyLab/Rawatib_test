#pragma once
#include <QDialog>
#include <QComboBox>
#include <QLabel>

// ── FirstRunDialog ─────────────────────────────────────────────────────────
//
// Shown once on first launch before MainWindow appears.
// Lets the admin choose their language and currency so the entire app
// is correctly configured from the very first interaction.
//
// Language is saved via SettingsManager::setLanguage() — main.cpp reads
// it immediately after this dialog closes and installs translators before
// constructing MainWindow, so the whole app starts in the chosen language.
//
// Currency is saved via CurrencyManager::setCurrent().
//
// After the user confirms, SettingsManager::setFirstRunComplete() is called
// and this dialog never appears again.

class FirstRunDialog : public QDialog {
    Q_OBJECT

public:
    explicit FirstRunDialog(QWidget* parent = nullptr);

    // Show only on first launch (SettingsManager::isFirstRun()).
    // Returns immediately if first-run setup is already done.
    static void showIfNeeded(QWidget* parent = nullptr);

private slots:
    void onLanguageChanged(int index);
    void onCurrencyChanged(int index);

private:
    QComboBox* m_langCombo = nullptr;
    QComboBox* m_combo     = nullptr;
    QLabel*    m_preview   = nullptr;
};
