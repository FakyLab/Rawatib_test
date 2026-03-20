#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>

// General-purpose password entry dialog.
// Use the static helpers for common cases.
class PinDialog : public QDialog {
    Q_OBJECT

public:
    static bool requestUnlock(QWidget* parent);
    static bool requestUnlock(QWidget* parent, const QString& prompt);
    static bool showSetPin(QWidget* parent);
    static bool showChangePin(QWidget* parent);
    static bool showRemovePin(QWidget* parent);

private:
    explicit PinDialog(const QString& title, const QString& prompt,
                       bool twoField = false, QWidget* parent = nullptr);
    QString pin1() const;
    QString pin2() const;
    void    showError(const QString& msg);

    QLineEdit* m_pin1Edit   = nullptr;
    QLineEdit* m_pin2Edit   = nullptr;
    QLabel*    m_errorLabel = nullptr;
};
