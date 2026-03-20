#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QRegularExpressionValidator>
#include <QCheckBox>
#include <QTimer>
#include <QString>
#include "models/AttendanceRecord.h"
#include "models/Employee.h"
#include "utils/LockPolicy.h"

class AttendanceTab : public QWidget {
    Q_OBJECT

public:
    explicit AttendanceTab(QWidget* parent = nullptr);

    void setEmployee(const Employee& employee, const QString& employeeName);
    void setMonth(int year, int month);
    void refresh();

signals:
    void attendanceChanged();
    void monthChanged(int year, int month);
    void lockIconClicked();
    void selfViewActivated(bool active);   // emitted when self-view state changes
    void adminUnlocked();                  // emitted after successful inline unlock

public slots:
    void onLockChanged(bool unlocked);
    void setSelfViewActive(bool active);   // called by MainWindow to sync from SalaryTab

private slots:
    void onAddRecord();
    void onEditRecord();
    void onDeleteRecord();
    void onMarkPaid();
    void onCheckIn();
    void onCheckOut();
    void onMonthChanged();
    void onContextMenu(const QPoint& pos);
    void onItemSelectionChanged();
    void onSelfViewToggled(bool checked);
    void onSelfViewTimeout();

private:
    void setupUi();
    void loadRecords();
    void refreshPayBtn();
    void refreshCheckButtons();
    void refreshLockIcon();
    void refreshSelfViewCheckbox();
    void refreshWageVisibility();
    void jumpToCurrentMonth();
    void populateTree(const QVector<AttendanceRecord>& records);
    void applyBannerStyles();         // re-applies banner stylesheets — called on theme change
    bool guardAdmin(LockPolicy::Feature feature);
    bool guardMarkPaid();
    bool guardKioskPin(LockPolicy::Feature feature);
    bool wagesVisible() const;        // true when wages should be shown

    QVector<int> selectedRecordIds() const;
    int currentRecordId() const;
    QVector<int> dayRecordIds(QTreeWidgetItem* dayItem) const;

    QComboBox*    m_monthCombo       = nullptr;
    QComboBox*    m_yearCombo        = nullptr;
    QPushButton*  m_lockBtn          = nullptr;
    QLabel*       m_wageBanner               = nullptr;
    QLabel*       m_noExpectedTimesBanner    = nullptr;
    QLabel*       m_mixedModesBanner         = nullptr;
    QTreeWidget*  m_tree             = nullptr;
    QPushButton*  m_addBtn           = nullptr;
    QPushButton*  m_editBtn          = nullptr;
    QPushButton*  m_deleteBtn        = nullptr;
    QPushButton*  m_markPaidBtn      = nullptr;
    QPushButton*  m_markMonthPaidBtn = nullptr;
    QPushButton*  m_checkInBtn       = nullptr;
    QPushButton*  m_checkOutBtn      = nullptr;
    QCheckBox*    m_selfViewCheck    = nullptr;   // "Show My Wages"
    QTimer*       m_selfViewTimer    = nullptr;   // 2-min inactivity reset

    int     m_employeeId    = -1;
    QString m_employeeName;
    Employee m_employee;    // full employee config for wage calculation
    int     m_year          = 0;
    int     m_month         = 0;
    bool    m_adminUnlocked = true;
    bool    m_selfViewActive = false;  // true when employee PIN verified this session
};
