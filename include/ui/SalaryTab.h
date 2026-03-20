#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QTimer>
#include "utils/PayrollCalculator.h"
#include "models/Employee.h"

class SalaryTab : public QWidget {
    Q_OBJECT

public:
    explicit SalaryTab(QWidget* parent = nullptr);

    void setEmployee(int employeeId, const QString& employeeName);
    void setMonth(int year, int month);
    void refresh();

signals:
    void monthChanged(int year, int month);
    void selfViewActivated(bool active);
    void lockIconClicked();               // routed to MainWindow like AttendanceTab
    void adminUnlocked();                 // emitted after successful inline unlock

public slots:
    void onRulesChanged();
    void onLockChanged(bool unlocked);
    void setSelfViewActive(bool active);
    void refreshLockBtn();                // called after LockPolicyDialog closes

private slots:
    void onMonthChanged();
    void onPrint();
    void onExport();
    void onRuleValueChanged(int row, int col);
    void onSelfViewToggled(bool checked);
    void onSelfViewTimeout();

private:
    void setupUi();
    void loadSummary();
    void updateSummaryCards();
    void updateNetPaySection();
    void refreshSelfViewCheckbox();
    void refreshLockIcon();               // updates lock button emoji + tooltip
    bool wagesVisible() const;

    // ── Period selector ───────────────────────────────────────────────────
    QComboBox* m_monthCombo = nullptr;
    QComboBox* m_yearCombo  = nullptr;

    // ── Self-view ─────────────────────────────────────────────────────────
    QCheckBox* m_selfViewCheck  = nullptr;
    QTimer*    m_selfViewTimer  = nullptr;

    // ── Gross summary cards ───────────────────────────────────────────────
    QLabel* m_totalDaysValue    = nullptr;
    QLabel* m_totalHoursValue   = nullptr;
    QLabel* m_totalSalaryValue  = nullptr;
    QLabel* m_paidAmountValue   = nullptr;
    QLabel* m_unpaidAmountValue = nullptr;

    // Monthly salary employees only — hidden for hourly
    QLabel* m_expectedSalaryLabel  = nullptr;
    QLabel* m_expectedSalaryValue  = nullptr;
    QLabel* m_absentDaysLabel      = nullptr;
    QLabel* m_absentDaysValue      = nullptr;
    QLabel* m_exceptionDaysLabel   = nullptr;   // approved off-days
    QLabel* m_exceptionDaysValue   = nullptr;
    QLabel* m_totalDeductionsLabel = nullptr;
    QLabel* m_totalDeductionsValue = nullptr;

    // ── Net pay section ───────────────────────────────────────────────────
    QGroupBox*    m_netGroup    = nullptr;
    QTableWidget* m_rulesTable  = nullptr;
    QLabel*       m_netPayValue = nullptr;

    // ── Buttons ───────────────────────────────────────────────────────────
    QPushButton* m_printBtn  = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_lockBtn   = nullptr;   // only visible when HideWages active

    // ── State ─────────────────────────────────────────────────────────────
    int     m_employeeId      = -1;
    QString m_employeeName;
    PayType m_employeePayType = PayType::Hourly;   // cached for rule filtering
    int     m_year          = 0;
    int     m_month         = 0;
    double  m_grossPay      = 0.0;
    bool    m_adminUnlocked = true;
    bool    m_selfViewActive = false;

    QVector<AppliedRule> m_appliedRules;
};
