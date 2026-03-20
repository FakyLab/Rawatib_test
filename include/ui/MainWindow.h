#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QTabWidget>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QTimer>
#include "utils/SessionManager.h"

class EmployeePanel;
class AttendanceTab;
class SalaryTab;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(bool devMode = false, const QString& dbPath = {},
                        QWidget* parent = nullptr);
    ~MainWindow() override = default;

    bool isAdminUnlocked() const { return SessionManager::isUnlocked(); }

public slots:
    void lockAdmin();
    void unlockAdmin();

signals:
    void adminLockChanged(bool unlocked);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onEmployeeSelected(int employeeId);
    void onAttendanceChanged();
    void onMonthChanged(int year, int month);
    void onBackupDatabase();
    void onRestoreDatabase();
    void onImportAttendance();
    void onExportAll();
    void onSetLanguage(const QString& langCode);
    void onSetCurrency();
    void onAdminPinSet();
    void onAdminPinChange();
    void onAdminPinRemove();
    void onGenerateRecoveryFile();
    void onSetSessionTimeout();
    void onSessionTimeout();
    void onCheckForUpdates();
    void onDevAppInfo();
    void onDevResetSettings();

private:
    void setupUi();
    void setupMenuBar();
    void updatePinMenu();
    void updateStatusBar();
    void updateBypassWarning();
    void resetSessionTimer();
    void applySessionTimeout();   // starts or stops timer based on current settings
    bool tryUnlockAdmin();        // inline unlock at point of action — returns true on success
    void showPostUnlockHint();    // shows the "Lock now" hint in status bar
    void hidePostUnlockHint();    // hides the hint (called on lock)
    void onUpdateFound(const QString& ver, const QString& url);
    void showUpdateDialog(const QString& ver, const QString& url,
                          const QString& notes);

    EmployeePanel* m_employeePanel       = nullptr;
    QTabWidget*    m_rightTabs           = nullptr;
    AttendanceTab* m_attendanceTab       = nullptr;
    SalaryTab*     m_salaryTab           = nullptr;
    QLabel*        m_statusLabel         = nullptr;
    QLabel*        m_bypassWarningLabel  = nullptr;
    QLabel*        m_lockStatusLabel     = nullptr;
    QLabel*        m_unlockHintLabel     = nullptr;  // "🔓 ... Lock now" hint

    QVector<QAction*> m_langActions;   // one entry per registered language, built in setupMenuBar()
    QAction* m_pinSingleAction     = nullptr;
    QMenu*   m_pinSubMenu          = nullptr;
    QAction* m_pinChangeAction     = nullptr;
    QAction* m_pinRemoveAction     = nullptr;
    QAction* m_pinRecoveryAction   = nullptr;
    QAction* m_timeoutAction       = nullptr;  // "Auto-lock after inactivity..."
    QAction* m_currencyAction      = nullptr;  // "Currency..."
    QMenu*   m_helpMenu            = nullptr;  // Help menu — title badged when update found
    QAction* m_checkUpdateAction   = nullptr;  // text changes to reflect update state

    QTimer*  m_sessionTimer        = nullptr;

    bool    m_devMode       = false;
    bool    m_adminUnlocked = false;
    QString m_dbPath;
    QString m_pendingUpdateVer;   // set when a new version is found, cleared after dialog
    QString m_pendingUpdateUrl;   // release page URL for the pending update

    int m_currentEmployeeId = -1;
    int m_currentYear  = 0;
    int m_currentMonth = 0;
};
