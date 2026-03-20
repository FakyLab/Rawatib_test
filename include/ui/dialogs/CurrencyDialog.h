#pragma once
#include <QDialog>
#include <QComboBox>
#include <QLabel>

// Settings dialog for choosing the active currency.
// Shows a searchable combo box with formatted names and a live preview.
class CurrencyDialog : public QDialog {
    Q_OBJECT

public:
    explicit CurrencyDialog(QWidget* parent = nullptr);

    // Show the dialog. Returns true if the user confirmed a change.
    static bool show(QWidget* parent);

private slots:
    void onCurrencyChanged(int index);

private:
    QComboBox* m_combo   = nullptr;
    QLabel*    m_preview = nullptr;
};
