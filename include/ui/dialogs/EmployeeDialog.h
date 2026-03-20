#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QRadioButton>
#include <QGroupBox>
#include <QCheckBox>
#include <QWidget>
#include <QStackedWidget>
#include "models/Employee.h"

class EmployeeDialog : public QDialog {
    Q_OBJECT

public:
    // What the caller should do with the PIN after accept()
    enum class PinAction { None, SetNew, Clear };

    explicit EmployeeDialog(QWidget* parent = nullptr);
    explicit EmployeeDialog(const Employee& employee, QWidget* parent = nullptr);

    Employee   employee()   const;
    PinAction  pinAction()  const { return m_pinAction; }
    QString    newPin()     const { return m_newPin; }

private slots:
    void onAccept();
    void onPayTypeChanged();
    void onWageChanged(double value);
    void onSalaryChanged(double value);
    void onSetPinClicked();
    void onClearPinClicked();
    void onExpectedTimesToggled(bool checked);

private:
    void setupUi();
    void populate(const Employee& employee);
    bool validate();

    // ── Basic fields ──────────────────────────────────────────────────────
    QLineEdit*   m_nameEdit   = nullptr;
    QLineEdit*   m_phoneEdit  = nullptr;
    QTextEdit*   m_notesEdit  = nullptr;

    // ── Pay type ──────────────────────────────────────────────────────────
    QRadioButton* m_hourlyRadio  = nullptr;
    QRadioButton* m_monthlyRadio = nullptr;

    // Hourly section
    QWidget*        m_hourlyWidget  = nullptr;
    QDoubleSpinBox* m_wageSpin      = nullptr;
    QLabel*         m_wageWarning   = nullptr;

    // Monthly section
    QWidget*        m_monthlyWidget      = nullptr;
    QDoubleSpinBox* m_salarySpin         = nullptr;
    QSpinBox*       m_workDaysSpin       = nullptr;
    QCheckBox*      m_expectedTimesCheck = nullptr;   // "Use expected times"
    QTimeEdit*      m_expectedCiEdit     = nullptr;
    QTimeEdit*      m_expectedCoEdit     = nullptr;
    QSpinBox*       m_lateTolSpin        = nullptr;
    QLabel*         m_salaryWarning      = nullptr;
    QLabel*         m_dailyRateLabel     = nullptr;

    // ── PIN section ───────────────────────────────────────────────────────
    QPushButton* m_setPinBtn   = nullptr;   // "Set employee PIN" / "Change employee PIN"
    QPushButton* m_clearPinBtn = nullptr;   // "Clear PIN" — only visible when PIN exists
    QWidget*     m_pinFields   = nullptr;   // New PIN + Confirm PIN (shown after clicking Set/Change)
    QLineEdit*   m_pinEdit     = nullptr;
    QLineEdit*   m_pinConfirm  = nullptr;
    QLabel*      m_pinError    = nullptr;

    // ── State ─────────────────────────────────────────────────────────────
    int       m_employeeId  = 0;
    bool      m_hasPinSet   = false;   // true when editing an employee that already has a PIN
    PinAction m_pinAction   = PinAction::None;
    QString   m_newPin;
};
