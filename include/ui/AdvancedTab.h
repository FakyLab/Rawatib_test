#pragma once
#include <QWidget>
#include <QCheckBox>
#include <QTableWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTabWidget>
#include "utils/DeductionPolicy.h"
#include "models/PayrollRule.h"

class AdvancedTab : public QWidget {
    Q_OBJECT

public:
    explicit AdvancedTab(QWidget* parent = nullptr);

    static bool isPayrollRulesEnabled();

signals:
    void rulesChanged();

public slots:
    void onEmployeeListChanged();

private slots:
    void onMasterToggled(bool enabled);

    // Rules CRUD — operate on whichever tab is active
    void onAddRule();
    void onEditRule();
    void onDeleteRule();
    void onSelectionChanged();

    // Day exceptions
    void onAddException();
    void onRemoveException();
    void onExceptionSelectionChanged();

    // Deduction policy
    void onDeductionModeChanged();
    void onPenaltyPctChanged(double pct);

private:
    void setupUi();
    void loadRulesAllTabs();
    void loadRulesForTable(QTableWidget* table, PayrollRule::AppliesTo tab);
    void loadExceptions();
    void loadDeductionPolicy();
    void setControlsEnabled(bool enabled);

    // Returns the active tab's AppliesTo value and table/buttons
    PayrollRule::AppliesTo activeTabAppliesTo() const;
    QTableWidget*          activeRulesTable()   const;
    QPushButton*           activeEditBtn()      const;
    QPushButton*           activeDeleteBtn()    const;

    // ── Master toggle ─────────────────────────────────────────────────────
    QCheckBox*   m_masterCheck = nullptr;

    // ── Tab widget ────────────────────────────────────────────────────────
    QTabWidget*  m_tabWidget   = nullptr;

    // ── All Employees tab (index 0) ───────────────────────────────────────
    QTableWidget* m_tableAll    = nullptr;
    QPushButton*  m_addBtnAll   = nullptr;
    QPushButton*  m_editBtnAll  = nullptr;
    QPushButton*  m_deleteBtnAll = nullptr;
    // Day Exceptions lives in this tab
    QTableWidget* m_exTable     = nullptr;
    QPushButton*  m_exAddBtn    = nullptr;
    QPushButton*  m_exRemoveBtn = nullptr;

    // ── Monthly Employees tab (index 1) ───────────────────────────────────
    // Deduction policy group lives here
    QGroupBox*      m_deductionGroup  = nullptr;
    QRadioButton*   m_radioPerMinute  = nullptr;
    QRadioButton*   m_radioPerDay     = nullptr;
    QRadioButton*   m_radioOff        = nullptr;
    QDoubleSpinBox* m_penaltySpin     = nullptr;
    QLabel*         m_penaltyLabel    = nullptr;
    QTableWidget*   m_tableMonthly    = nullptr;
    QPushButton*    m_addBtnMonthly   = nullptr;
    QPushButton*    m_editBtnMonthly  = nullptr;
    QPushButton*    m_deleteBtnMonthly = nullptr;

    // ── Hourly Employees tab (index 2) ────────────────────────────────────
    QTableWidget* m_tableHourly    = nullptr;
    QPushButton*  m_addBtnHourly   = nullptr;
    QPushButton*  m_editBtnHourly  = nullptr;
    QPushButton*  m_deleteBtnHourly = nullptr;
};
