#include "ui/MainWindow.h"
#include "ui/dialogs/PayrollRulesDialog.h"
#include "ui/dialogs/LockPolicyDialog.h"
#include "ui/EmployeePanel.h"
#include "ui/AttendanceTab.h"
#include "ui/SalaryTab.h"
#include "ui/dialogs/PinDialog.h"
#include "ui/dialogs/CurrencyDialog.h"
#include "repositories/EmployeeRepository.h"
#include "database/DatabaseManager.h"
#include "database/AutoBackupManager.h"
#include <QTimer>
#include <QEvent>
#include <QSpinBox>
#include "utils/SettingsManager.h"
#include "utils/PinManager.h"
#include "utils/CurrencyManager.h"
#include "utils/LanguageRegistry.h"
#include "utils/ImportHelper.h"
#include "utils/ExportHelper.h"
#include "utils/LockPolicy.h"
#include "utils/AuditLog.h"
#include "utils/SessionManager.h"
#include "ui/dialogs/AuditLogDialog.h"
#include "ui/dialogs/TipsDialog.h"
#include "ui/dialogs/LicenseDialog.h"
#include "ui/dialogs/DatabaseUnlockDialog.h"
#include "utils/UpdateChecker.h"
#include "utils/ThemeHelper.h"
#include <QSplitter>
#include <QMenuBar>
#include <QStatusBar>
#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QActionGroup>
#include <QMessageBox>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QClipboard>
#include <QDialog>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QGuiApplication>
#include <QFont>
#include <QCheckBox>

MainWindow::MainWindow(bool devMode, const QString& dbPath, QWidget* parent)
    : QMainWindow(parent), m_devMode(devMode), m_dbPath(dbPath)
{
    m_currentYear  = QDate::currentDate().year();
    m_currentMonth = QDate::currentDate().month();

    // Initialize session binding before any lock/unlock calls
    SessionManager::init();
    if (!PinManager::isPinSet())
        SessionManager::setUnlocked(true);

    // Start locked if PIN is set, unlocked otherwise
    m_adminUnlocked = !PinManager::isPinSet();

    setupUi();
    setupMenuBar();
    setWindowTitle(tr("Rawatib"));
    setMinimumSize(960, 640);
    resize(1100, 700);

    updateStatusBar();
    updateBypassWarning();

    // ── Session timeout timer ─────────────────────────────────────────────
    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setSingleShot(true);
    connect(m_sessionTimer, &QTimer::timeout, this, &MainWindow::onSessionTimeout);
    qApp->installEventFilter(this);
    applySessionTimeout();

    // Re-apply theme-sensitive stylesheets when the system theme changes at runtime
    ThemeHelper::onThemeChanged(this, [this]() { updateBypassWarning(); });

    const QString backupMsg = AutoBackupManager::instance().lastMessage();
    if (!backupMsg.isEmpty())
        statusBar()->showMessage(tr("Auto-backup: %1").arg(backupMsg), 6000);

    // ── Silent update check ───────────────────────────────────────────────
    // Fires 3 seconds after launch so it never competes with startup work.
    // Only badges the Help menu if a newer version exists.
    // Does nothing on network failure — user is not expecting a result.
    // Skips silently if the user already dismissed this version.
    QTimer::singleShot(3000, this, [this]() {
        const QString dismissed =
            SettingsManager::value("lastDismissedUpdate").toString();

        auto* checker = new UpdateChecker(this);
        connect(checker, &UpdateChecker::updateAvailable,
                this, [this, dismissed](const QString& ver,
                                        const QString& url,
                                        const QString& /*notes*/) {
            if (ver == dismissed) return;
            onUpdateFound(ver, url);
        });
        checker->check(/*silent=*/true);
    });
}

// ── Lock / Unlock ──────────────────────────────────────────────────────────

void MainWindow::lockAdmin() {
    if (!m_adminUnlocked) return;
    m_adminUnlocked = false;
    SessionManager::setUnlocked(false);
    if (m_sessionTimer) m_sessionTimer->stop();
    hidePostUnlockHint();
    updateStatusBar();
    updatePinMenu();
    if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
    emit adminLockChanged(false);
}

void MainWindow::unlockAdmin() {
    if (m_adminUnlocked) return;
    if (!PinManager::isPinSet()) {
        m_adminUnlocked = true;
        SessionManager::setUnlocked(true);
        applySessionTimeout();
        updateStatusBar();
        updatePinMenu();
        if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
        emit adminLockChanged(true);
        return;
    }
    if (PinDialog::requestUnlock(this)) {
        m_adminUnlocked = true;
        SessionManager::setUnlocked(true);
        applySessionTimeout();
        updateStatusBar();
        updatePinMenu();
        if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
        emit adminLockChanged(true);
    }
}

// ── tryUnlockAdmin ────────────────────────────────────────────────────────
// Called by inline guard paths (menu actions and child widget signals).
// If already unlocked: returns true immediately, no hint shown.
// If no PIN set: unlocks silently, returns true.
// Otherwise: shows PinDialog inline, on success syncs all state,
// shows the post-unlock hint, and returns true.
// Returns false if the user cancelled or entered wrong password.

bool MainWindow::tryUnlockAdmin() {
    if (m_adminUnlocked) return true;

    if (!PinManager::isPinSet()) {
        m_adminUnlocked = true;
        SessionManager::setUnlocked(true);
        applySessionTimeout();
        updateStatusBar();
        updatePinMenu();
        if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
        emit adminLockChanged(true);
        return true;
    }

    // Auth already happened in the child widget's guardAdmin — SessionManager
    // reflects unlocked state. If it's now unlocked, just sync MainWindow.
    if (SessionManager::isUnlocked()) {
        m_adminUnlocked = true;
        applySessionTimeout();
        updateStatusBar();
        updatePinMenu();
        if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
        emit adminLockChanged(true);
        showPostUnlockHint();
        return true;
    }

    // Called from menu action — do the auth here
    if (PinDialog::requestUnlock(this)) {
        m_adminUnlocked = true;
        SessionManager::setUnlocked(true);
        applySessionTimeout();
        updateStatusBar();
        updatePinMenu();
        if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
        emit adminLockChanged(true);
        showPostUnlockHint();
        return true;
    }
    return false;
}

// ── showPostUnlockHint / hidePostUnlockHint ───────────────────────────────
// The hint label lives in the status bar between the bypass warning and the
// lock state label. It appears when an inline unlock caused the session to
// become unlocked, and disappears when the session is locked again.
// "Lock now" is a rich-text hyperlink that calls lockAdmin() directly.

void MainWindow::showPostUnlockHint() {
    if (!m_unlockHintLabel) return;
    if (!PinManager::isPinSet()) return;   // no PIN — lock concept doesn't apply
    const bool autoLockEnabled = SettingsManager::getSessionTimeout() > 0;
    m_unlockHintLabel->setText(
        autoLockEnabled
            ? tr("<b>Admin session unlocked</b> · <b>Auto-locks after inactivity</b> · "
                 "<a href='lock'>Lock now</a>")
            : tr("<b>Admin session unlocked</b> · <b>Auto-lock disabled</b> · "
                 "<a href='lock'>Lock now</a>"));
    m_unlockHintLabel->show();
}

void MainWindow::hidePostUnlockHint() {
    if (!m_unlockHintLabel) return;
    m_unlockHintLabel->hide();
    m_unlockHintLabel->clear();
}

void MainWindow::updateStatusBar() {
    if (!m_lockStatusLabel) return;
    if (!PinManager::isPinSet()) {
        m_lockStatusLabel->setText(QString());
        m_lockStatusLabel->hide();
    } else if (m_adminUnlocked) {
        m_lockStatusLabel->setText(tr("🔓 Admin access unlocked"));
        m_lockStatusLabel->show();
    } else {
        m_lockStatusLabel->setText(tr("🔒 Admin access locked"));
        m_lockStatusLabel->show();
    }
}

void MainWindow::updateBypassWarning() {
    if (!m_bypassWarningLabel) return;

    const QString ts = PinManager::getBypassTimestamp();
    if (ts.isEmpty()) {
        m_bypassWarningLabel->hide();
        m_bypassWarningLabel->clear();
        return;
    }

    // Format timestamp for display — respects Arabic/English locale
    const QDateTime dt = QDateTime::fromString(ts, Qt::ISODate);
    const QString dtStr = dt.isValid()
        ? QLocale().toString(dt, tr("MMM d 'at' h:mm AP"))
        : ts;

    if (!PinManager::isPinSet()) {
        // PIN is missing — red, no dismiss
        m_bypassWarningLabel->setStyleSheet(
            ThemeHelper::isDark()
            ? "color: #ef9a9a; font-weight: bold;"
            : "color: #c0392b; font-weight: bold;");
        m_bypassWarningLabel->setText(
            tr("⚠  Admin password was removed via emergency bypass on %1. "
               "Set a new password in Settings — then generate a new recovery file.")
               .arg(dtStr));
        m_bypassWarningLabel->show();

    } else if (PinManager::bypassPinWasSet()) {
        // PIN was set during the bypass session — blue, dismissable
        m_bypassWarningLabel->setStyleSheet(
            ThemeHelper::isDark()
            ? "color: #90caf9; font-weight: bold;"
            : "color: #0055aa; font-weight: bold;");
        m_bypassWarningLabel->setText(
            tr("⚠  Emergency bypass was used on %1. &nbsp;"
               "<a href='dismiss'>Dismiss</a>").arg(dtStr));
        m_bypassWarningLabel->show();

    } else {
        // PIN was set in a later normal session — admin already saw red warning
        // Silent cleanup
        PinManager::clearBypassRecord();
        m_bypassWarningLabel->hide();
        m_bypassWarningLabel->clear();
    }
}

// ── UI setup ──────────────────────────────────────────────────────────────

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    // Left: employee panel
    auto* empGroup = new QGroupBox(tr("Employees"), central);
    auto* empGroupLayout = new QVBoxLayout(empGroup);
    empGroupLayout->setContentsMargins(4, 4, 4, 4);
    m_employeePanel = new EmployeePanel(this);
    empGroupLayout->addWidget(m_employeePanel);
    rootLayout->addWidget(empGroup);

    // Right side
    auto* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(4);

    m_statusLabel = new QLabel(tr("Select an employee to get started"), central);
    rightLayout->addWidget(m_statusLabel);

    m_rightTabs     = new QTabWidget(central);
    m_attendanceTab = new AttendanceTab(this);
    m_salaryTab     = new SalaryTab(this);

    m_rightTabs->addTab(m_attendanceTab, tr("Attendance"));
    m_rightTabs->addTab(m_salaryTab,     tr("Salary Summary"));
    rightLayout->addWidget(m_rightTabs, 1);
    rootLayout->addLayout(rightLayout, 1);

    // Status bar — bypass warning on left (stretches), lock state on right
    m_bypassWarningLabel = new QLabel(this);
    m_bypassWarningLabel->setTextFormat(Qt::RichText);
    m_bypassWarningLabel->setOpenExternalLinks(false);
    m_bypassWarningLabel->hide();
    connect(m_bypassWarningLabel, &QLabel::linkActivated, this, [this]() {
        PinManager::clearBypassRecord();
        updateBypassWarning();
    });
    statusBar()->insertPermanentWidget(0, m_bypassWarningLabel, 1);

    // ── Unlock hint label (shown after inline unlock, hidden when locked) ─
    m_unlockHintLabel = new QLabel(this);
    m_unlockHintLabel->setTextFormat(Qt::RichText);
    m_unlockHintLabel->setOpenExternalLinks(false);
    m_unlockHintLabel->hide();
    connect(m_unlockHintLabel, &QLabel::linkActivated, this, [this]() {
        lockAdmin();
    });
    statusBar()->insertPermanentWidget(1, m_unlockHintLabel, 1);

    m_lockStatusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_lockStatusLabel);
    statusBar()->showMessage(tr("Ready  |  Rawatib v%1")
        .arg(QCoreApplication::applicationVersion()));

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_employeePanel, &EmployeePanel::employeeSelected,
            this, &MainWindow::onEmployeeSelected);
    connect(m_employeePanel, &EmployeePanel::employeeListChanged, this, [this]() {
        if (m_currentEmployeeId > 0) onEmployeeSelected(m_currentEmployeeId);
    });
    connect(m_attendanceTab, &AttendanceTab::attendanceChanged,
            this, &MainWindow::onAttendanceChanged);
    connect(m_attendanceTab, &AttendanceTab::monthChanged,
            this, &MainWindow::onMonthChanged);
    connect(m_salaryTab, &SalaryTab::monthChanged, this, [this](int y, int m) {
        m_currentYear = y; m_currentMonth = m;
        m_attendanceTab->setMonth(y, m);
    });

    // Wire lock signal to child widgets
    connect(this, &MainWindow::adminLockChanged,
            m_attendanceTab, &AttendanceTab::onLockChanged);
    connect(this, &MainWindow::adminLockChanged,
            m_employeePanel, &EmployeePanel::onLockChanged);
    connect(this, &MainWindow::adminLockChanged,
            m_salaryTab, &SalaryTab::onLockChanged);

    // Wire inline unlock signals — child unlocked admin, sync MainWindow state
    connect(m_employeePanel, &EmployeePanel::adminUnlocked,
            this, [this]() { tryUnlockAdmin(); });
    connect(m_attendanceTab, &AttendanceTab::adminUnlocked,
            this, [this]() { tryUnlockAdmin(); });
    connect(m_salaryTab, &SalaryTab::adminUnlocked,
            this, [this]() { tryUnlockAdmin(); });

    // ── Self-view sync — checking in one tab activates the other ──────────
    connect(m_attendanceTab, &AttendanceTab::selfViewActivated,
            m_salaryTab,     &SalaryTab::setSelfViewActive);
    connect(m_salaryTab,     &SalaryTab::selfViewActivated,
            m_attendanceTab, &AttendanceTab::setSelfViewActive);

    // Wire lock icon click from AttendanceTab
    connect(m_attendanceTab, &AttendanceTab::lockIconClicked, this, [this]() {
        if (m_adminUnlocked) lockAdmin();
        else                 unlockAdmin();
    });
    connect(m_salaryTab, &SalaryTab::lockIconClicked, this, [this]() {
        if (m_adminUnlocked) lockAdmin();
        else                 unlockAdmin();
    });
}

// ── Session timeout ───────────────────────────────────────────────────────

void MainWindow::applySessionTimeout() {
    if (!m_sessionTimer) return;
    const int minutes = SettingsManager::getSessionTimeout();
    if (m_adminUnlocked && PinManager::isPinSet() && minutes > 0) {
        m_sessionTimer->start(minutes * 60 * 1000);
    } else {
        m_sessionTimer->stop();
    }
}

void MainWindow::resetSessionTimer() {
    if (m_adminUnlocked && PinManager::isPinSet() &&
        SettingsManager::getSessionTimeout() > 0 && m_sessionTimer) {
        m_sessionTimer->start(SettingsManager::getSessionTimeout() * 60 * 1000);
    }
}

void MainWindow::onSessionTimeout() {
    if (!m_adminUnlocked) return;
    lockAdmin();
    statusBar()->showMessage(tr("Admin session timed out due to inactivity."), 8000);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // Reset inactivity timer on any user interaction
    if (m_adminUnlocked && PinManager::isPinSet()) {
        const auto t = event->type();
        if (t == QEvent::MouseMove     ||
            t == QEvent::MouseButtonPress ||
            t == QEvent::KeyPress      ||
            t == QEvent::Wheel) {
            resetSessionTimer();
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onSetSessionTimeout() {
    const int current = SettingsManager::getSessionTimeout();

    // Custom dialog instead of QInputDialog so we can enforce a minimum width
    // that prevents the Arabic title from being truncated in the title bar.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Auto-lock Timeout"));
    dlg.setMinimumWidth(360);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 12);

    auto* label = new QLabel(
        tr("Lock admin session after inactivity (minutes).\n"
           "Set to 0 to disable auto-lock."), &dlg);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* spin = new QSpinBox(&dlg);
    spin->setRange(0, 60);
    spin->setValue(current);
    spin->setSpecialValueText(tr("Disabled (0)"));
    layout->addWidget(spin);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const int minutes = spin->value();
    SettingsManager::setSessionTimeout(minutes);
    applySessionTimeout();
    if (minutes == 0)
        statusBar()->showMessage(tr("Auto-lock disabled."), 4000);
    else
        statusBar()->showMessage(
            tr("Admin will be locked after %1 minute(s) of inactivity.").arg(minutes), 4000);
    // Refresh the hint label live if the admin session is currently unlocked
    if (m_adminUnlocked) showPostUnlockHint();
}

// ── Menu bar ──────────────────────────────────────────────────────────────

void MainWindow::setupMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* backupAction = fileMenu->addAction(tr("&Backup Database..."));
    backupAction->setShortcut(QKeySequence("Ctrl+B"));
    connect(backupAction, &QAction::triggered, this, [this]() {
        if (LockPolicy::isLocked(LockPolicy::Feature::BackupDatabase)
                && !SessionManager::isUnlocked() && PinManager::isPinSet()) {
            if (!tryUnlockAdmin()) return;
        }
        onBackupDatabase();
    });

    auto* restoreAction = fileMenu->addAction(tr("&Restore Database..."));
    restoreAction->setShortcut(QKeySequence("Ctrl+R"));
    connect(restoreAction, &QAction::triggered, this, [this]() {
        if (LockPolicy::isLocked(LockPolicy::Feature::RestoreDatabase)
                && !SessionManager::isUnlocked() && PinManager::isPinSet()) {
            if (!tryUnlockAdmin()) return;
        }
        onRestoreDatabase();
    });

    auto* openFolderAction = fileMenu->addAction(tr("&Open Auto-Backup Folder"));
    connect(openFolderAction, &QAction::triggered, this, []() {
        const QString folder = AutoBackupManager::backupFolder();
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });

    fileMenu->addSeparator();

    auto* importAction = fileMenu->addAction(tr("Import Attendance..."));
    importAction->setShortcut(QKeySequence("Ctrl+I"));
    connect(importAction, &QAction::triggered, this, [this]() {
        if (LockPolicy::isLocked(LockPolicy::Feature::ImportAttendance)
                && !SessionManager::isUnlocked() && PinManager::isPinSet()) {
            if (!tryUnlockAdmin()) return;
        }
        onImportAttendance();
    });

    auto* exportAllAction = fileMenu->addAction(tr("Export All Employees..."));
    exportAllAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(exportAllAction, &QAction::triggered, this, [this]() {
        onExportAll();
    });

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    // ── View menu ──────────────────────────────────────────────────────────
    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    // Language submenu — built dynamically from LanguageRegistry.
    // Adding a new language only requires updating LanguageRegistry.cpp
    // and dropping the .ts/.qm files into translations/.
    auto* langMenu    = viewMenu->addMenu(tr("Language"));
    auto* langGroup   = new QActionGroup(this);
    langGroup->setExclusive(true);
    const QString currentLang = SettingsManager::getLanguage();

    for (const LanguageInfo& lang : LanguageRegistry::all()) {
        auto* action = langMenu->addAction(lang.nativeName);
        action->setCheckable(true);
        action->setChecked(lang.code == currentLang);
        langGroup->addAction(action);
        const QString code = lang.code;   // capture by value for lambda
        connect(action, &QAction::triggered, this, [this, code]() {
            onSetLanguage(code);
        });
        m_langActions.append(action);
    }

    viewMenu->addSeparator();

    m_currencyAction = viewMenu->addAction(tr("Currency..."));
    connect(m_currencyAction, &QAction::triggered, this, &MainWindow::onSetCurrency);

    // ── Settings menu ──────────────────────────────────────────────────────
    auto* settingsMenu = menuBar()->addMenu(tr("Settings"));

    // Password menu — built once, updated dynamically
    m_pinSingleAction = settingsMenu->addAction(tr("Admin Password..."));
    connect(m_pinSingleAction, &QAction::triggered, this, &MainWindow::onAdminPinSet);

    m_pinSubMenu = new QMenu(tr("Admin Password"), this);
    m_pinChangeAction   = m_pinSubMenu->addAction(tr("Change Password..."));
    m_pinRemoveAction   = m_pinSubMenu->addAction(tr("Remove Password..."));
    m_pinSubMenu->addSeparator();
    m_pinRecoveryAction = m_pinSubMenu->addAction(tr("Generate Recovery File..."));
    connect(m_pinChangeAction,   &QAction::triggered, this, &MainWindow::onAdminPinChange);
    connect(m_pinRemoveAction,   &QAction::triggered, this, &MainWindow::onAdminPinRemove);
    connect(m_pinRecoveryAction, &QAction::triggered, this, &MainWindow::onGenerateRecoveryFile);
    settingsMenu->addMenu(m_pinSubMenu);

    updatePinMenu();

    settingsMenu->addSeparator();

    m_timeoutAction = settingsMenu->addAction(tr("Auto-lock Timeout..."));
    connect(m_timeoutAction, &QAction::triggered, this, &MainWindow::onSetSessionTimeout);

    // ── Advanced menu ──────────────────────────────────────────────────────
    auto* advancedMenu = menuBar()->addMenu(tr("Advanced"));
    auto* payrollRulesAction = advancedMenu->addAction(tr("Payroll Rules..."));
    connect(payrollRulesAction, &QAction::triggered, this, [this]() {
        if (LockPolicy::isLocked(LockPolicy::Feature::PayrollRules)
                && !SessionManager::isUnlocked() && PinManager::isPinSet()) {
            if (!tryUnlockAdmin()) return;
        }
        PayrollRulesDialog dlg(this);
        connect(&dlg, &PayrollRulesDialog::rulesChanged,
                m_salaryTab, &SalaryTab::onRulesChanged);
        dlg.exec();
    });

    advancedMenu->addSeparator();

    auto* lockPolicyAction = advancedMenu->addAction(tr("Kiosk && Lock Policy..."));
    connect(lockPolicyAction, &QAction::triggered, this, [this]() {
        if (!SessionManager::isUnlocked() && PinManager::isPinSet()) {
            if (!tryUnlockAdmin()) return;
        }
        LockPolicyDialog dlg(this);
        dlg.exec();
        // Refresh SalaryTab lock button — HideWages may have been toggled
        m_salaryTab->refreshLockBtn();
    });

    advancedMenu->addSeparator();

    auto* auditAction = advancedMenu->addAction(tr("Audit Log..."));
    connect(auditAction, &QAction::triggered, this, [this]() {
        if (!SessionManager::isUnlocked() && PinManager::isPinSet()) {
            if (!tryUnlockAdmin()) return;
        }
        AuditLogDialog dlg(this);
        dlg.exec();
    });

    // ── Help menu ──────────────────────────────────────────────────────────
    // Stored as a member so we can badge the title when an update is found.
    m_helpMenu = menuBar()->addMenu(tr("&Help"));

    m_helpMenu->addAction(tr("Discover Rawatib"), this, [this]() {
        TipsDialog dlg(this);
        dlg.exec();
    });

    // "Check for Updates" — text changes to reflect update state:
    //   Normal:       "Check for Updates"
    //   Checking:     "Checking..."           (briefly, while request is in flight)
    //   Update found: "Update Available — vX.Y.Z  🆕"
    //   Up to date:   "Check for Updates  ✓"  (resets after 4 s)
    m_checkUpdateAction = m_helpMenu->addAction(
        tr("Check for Updates"), this, &MainWindow::onCheckForUpdates);

    m_helpMenu->addAction(tr("License"), this, [this]() {
        LicenseDialog dlg(this);
        dlg.exec();
    });

    m_helpMenu->addSeparator();

    m_helpMenu->addAction(tr("&About"), this, [this]() {
        QMessageBox::about(this, tr("About Rawatib"),
            tr("<b>Rawatib</b> — v%1<br>"
               "Employee Attendance &amp; Payroll Manager<br><br>"
               "Track employee attendance, calculate wages, and manage salary payments.<br><br>"
               "© 2026 Rawatib<br>"
               "Mohammed Faky")
            .arg(QCoreApplication::applicationVersion()));
    });

    // ── Developer menu ────────────────────────────────────────────────────
    if (m_devMode) {
        auto* devMenu = menuBar()->addMenu(tr("Developer"));
        auto* appInfoAction = devMenu->addAction(tr("App Info..."));
        connect(appInfoAction, &QAction::triggered, this, &MainWindow::onDevAppInfo);
        auto* resetAction = devMenu->addAction(tr("Reset Settings..."));
        connect(resetAction, &QAction::triggered, this, &MainWindow::onDevResetSettings);
    }
}

void MainWindow::updatePinMenu() {
    const bool pinSet     = PinManager::isPinSet();
    const bool unlocked   = m_adminUnlocked;
    m_pinSingleAction->setVisible(!pinSet);
    m_pinSubMenu->menuAction()->setVisible(pinSet);
    // Recovery file only accessible when PIN is set and admin is unlocked
    if (m_pinRecoveryAction)
        m_pinRecoveryAction->setEnabled(pinSet && unlocked);
}

// ── PIN slots ─────────────────────────────────────────────────────────────

void MainWindow::onGenerateRecoveryFile() {
    if (!PinManager::isPinSet()) return;

    // Re-verify even when admin is unlocked — the recovery file grants
    // emergency database access, so it warrants an explicit confirmation.
    if (!PinDialog::requestUnlock(this,
            tr("Generating a recovery file requires your password.\n\n"
               "This file grants emergency access to the database if you "
               "ever forget your password. Keep it in a safe place.")))
        return;

    const QByteArray fileData = PinManager::generateRecoveryFileData();
    if (fileData.isEmpty()) {
        QMessageBox::warning(this, tr("Recovery File"),
            tr("Could not generate recovery file.\n"
               "The install secret may be missing from the database."));
        return;
    }

    const QString savePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Recovery File"),
        QDir::homePath() + "/" + PinManager::recoveryFileName(),
        tr("Recovery Files (*.rwtrec);;All Files (*)"));

    if (savePath.isEmpty()) return;

    QFile f(savePath);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Recovery File"),
            tr("Could not write to the selected file.\n%1").arg(f.errorString()));
        return;
    }
    f.write(fileData);
    f.close();

    QMessageBox::information(this, tr("Recovery File Saved"),
        tr("Recovery file saved to:\n%1\n\n"
           "Keep it safe — you need it for emergency access (--bypass-key).\n"
           "This file remains valid even if you change your password.").arg(savePath));
}

void MainWindow::onAdminPinSet() {
    // PinDialog::showSetPin internally calls PinManager::setPin()
    // which derives and stores the key in the OS keychain.
    // After that succeeds we rekey the DB from fallbackKey() → user key.
    if (PinDialog::showSetPin(this)) {
        if (!PinManager::getBypassTimestamp().isEmpty())
            PinManager::setBypassPinWasSet(true);

        // If loadKey() returns empty the keychain write silently failed —
        // roll back the PIN hash so the app stays in a consistent state.
        const QByteArray newKey = PinManager::loadKey();
        if (newKey.isEmpty()) {
            QMessageBox::critical(this, tr("Encryption Error"),
                tr("Password was set but the encryption key could not be stored "
                   "in the system keychain.\n\n"
                   "The password has been removed. Please try again or check "
                   "your system keychain settings."));
            PinManager::forceRemovePin();
            return;
        }

        // Rekey: fallbackKey() → newKey (PRAGMA rekey, no file ops)
        if (!DatabaseManager::instance().encryptDatabase(newKey)) {
            QMessageBox::critical(this, tr("Encryption Error"),
                tr("Password was set but the database could not be rekeyed:\n%1\n\n"
                   "The password has been removed to keep the app in a consistent state.")
                .arg(DatabaseManager::instance().lastError()));
            PinManager::forceRemovePin();
            return;
        }

        QMessageBox::information(this, tr("Password Set"),
            tr("Admin password has been set.\n"
               "The app is now locked. Click the lock icon to unlock."));
        m_adminUnlocked = false;
        SessionManager::setUnlocked(false);
        updatePinMenu();
        updateStatusBar();
        updateBypassWarning();
        emit adminLockChanged(false);
    }
}

void MainWindow::onAdminPinChange() {
    // PinDialog::showChangePin calls PinManager::changePin() which stores the
    // new DPAPI-wrapped key. We then rekey the open DB from old→new key.
    // We need the old key before the dialog runs so capture it first.
    const QByteArray oldKey = PinManager::loadKey();

    if (PinDialog::showChangePin(this)) {
        const QByteArray newKey = PinManager::loadKey();
        if (!oldKey.isEmpty() && !newKey.isEmpty()) {
            if (!DatabaseManager::instance().rekeyDatabase(oldKey, newKey)) {
                QMessageBox::critical(this, tr("Rekey Error"),
                    tr("Password was changed but the database could not be rekeyed:\n%1")
                    .arg(DatabaseManager::instance().lastError()));
                return;
            }
        }
        QMessageBox::information(this, tr("Password Changed"),
            tr("Admin password has been changed and the database has been rekeyed."));
    }
}

void MainWindow::onAdminPinRemove() {
    // PinDialog::showRemovePin calls PinManager::removePin() which deletes
    // the stored key from the keychain.  We capture the key before the dialog
    // runs — decryptDatabase() doesn't actually need it (it rekeys to the
    // compiled-in fallback), but we keep the capture for the consistency
    // check below.
    const QByteArray currentKey = PinManager::loadKey();

    if (PinDialog::showRemovePin(this)) {
        // Rekey: currentKey → fallbackKey() (PRAGMA rekey, no file ops)
        // This cannot corrupt the database.
        if (!currentKey.isEmpty()) {
            if (!DatabaseManager::instance().decryptDatabase(currentKey)) {
                QMessageBox::critical(this, tr("Rekey Error"),
                    tr("Password was removed but the database could not be "
                       "rekeyed to the default key:\n%1")
                    .arg(DatabaseManager::instance().lastError()));
                return;
            }
        }
        QMessageBox::information(this, tr("Password Removed"),
            tr("Admin password has been removed.\n"
               "All operations are now unrestricted."));
        m_adminUnlocked = true;
        SessionManager::setUnlocked(true);
        updatePinMenu();
        updateStatusBar();
        updateBypassWarning();
        emit adminLockChanged(true);
    }
}

// ── Currency ──────────────────────────────────────────────────────────────

void MainWindow::onSetCurrency() {
    if (CurrencyDialog::show(this)) {
        if (m_attendanceTab) m_attendanceTab->refresh();
        if (m_salaryTab)     m_salaryTab->refresh();
        updateStatusBar();
        statusBar()->showMessage(
            tr("Currency changed to %1 (%2).")
                .arg(CurrencyManager::current().englishName)
                .arg(CurrencyManager::symbol()), 4000);
    }
}

// ── Language ──────────────────────────────────────────────────────────────

void MainWindow::onSetLanguage(const QString& langCode) {
    const QString previousLang = SettingsManager::getLanguage();
    if (langCode == previousLang) return;

    SettingsManager::setLanguage(langCode);
    auto reply = QMessageBox::question(this,
        tr("Restart Required"),
        tr("Rawatib needs to restart to apply the language change.\nRestart now?"),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
        QApplication::quit();
    } else {
        // User declined — revert the saved setting back to what was running.
        SettingsManager::setLanguage(previousLang);

        // Restore checkmarks to reflect the still-running language.
        const QVector<LanguageInfo>& langs = LanguageRegistry::all();
        for (int i = 0; i < m_langActions.size() && i < langs.size(); ++i)
            m_langActions[i]->setChecked(langs[i].code == previousLang);
    }
}

// ── File operations ───────────────────────────────────────────────────────

void MainWindow::onBackupDatabase() {
    const QString defaultName = QString("attendance_backup_%1.db")
        .arg(QDate::currentDate().toString("yyyy-MM-dd"));
    const QString dest = QFileDialog::getSaveFileName(
        this, tr("Backup Database"),
        QDir::homePath() + "/" + defaultName,
        tr("SQLite Database (*.db);;All Files (*)"));
    if (dest.isEmpty()) return;

    const QString dbPath = DatabaseManager::instance().database().databaseName();
    DatabaseManager::instance().database().close();
    bool ok = QFile::copy(dbPath, dest);
    if (!ok && QFile::exists(dest)) { QFile::remove(dest); ok = QFile::copy(dbPath, dest); }
    DatabaseManager::instance().database().open();
    DatabaseManager::instance().reapplyKey();   // SQLCipher requires PRAGMA key after every open()

    if (ok) {
        AuditLog::record(AuditLog::BACKUP, "database", 0,
            QString("Database backed up to: %1").arg(dest));
        QMessageBox::information(this, tr("Backup Successful"),
            tr("Database backed up successfully to:\n%1").arg(dest));
        statusBar()->showMessage(tr("Backup saved: %1").arg(dest), 5000);
    } else {
        QMessageBox::critical(this, tr("Backup Failed"),
            tr("Could not write backup file to:\n%1").arg(dest));
    }
}

void MainWindow::onRestoreDatabase() {
    const QString src = QFileDialog::getOpenFileName(
        this, tr("Restore Database"), QDir::homePath(),
        tr("SQLite Database (*.db);;All Files (*)"));
    if (src.isEmpty()) return;

    auto reply = QMessageBox::warning(this, tr("Confirm Restore"),
        tr("Restoring will <b>replace all current data</b> with the contents of:<br><br>"
           "<b>%1</b><br><br>"
           "This cannot be undone. Continue?").arg(src),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    const QString dbPath  = DatabaseManager::instance().database().databaseName();
    const QString tmpPath = dbPath + ".tmp_before_restore";

    // Close DB, swap in the restored file
    DatabaseManager::instance().database().close();
    QFile::remove(tmpPath);
    QFile::copy(dbPath, tmpPath);
    QFile::remove(dbPath);
    const bool copied = QFile::copy(src, dbPath);

    if (!copied) {
        // Copy failed — roll back immediately
        QFile::remove(dbPath);
        QFile::copy(tmpPath, dbPath);
        QFile::remove(tmpPath);
        DatabaseManager::instance().database().open();
        DatabaseManager::instance().reapplyKey();
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Could not restore from:\n%1\n\nYour original data has been preserved.").arg(src));
        return;
    }

    // Reopen and try to apply the current keychain key
    DatabaseManager::instance().database().open();
    const bool keyOk = DatabaseManager::instance().reapplyKey();

    if (!keyOk) {
        // The restored DB was encrypted with a different key.
        // Ask the admin for the PIN that was used when this backup was made.
        QMessageBox::information(this, tr("Password Required"),
            tr("The restored database was encrypted with a different password.\n\n"
               "Enter the password that was set when this backup was made."));

        if (!DatabaseUnlockDialog::unlock(dbPath, this)) {
            // Admin cancelled — roll back to the original DB
            DatabaseManager::instance().database().close();
            QFile::remove(dbPath);
            QFile::copy(tmpPath, dbPath);
            QFile::remove(tmpPath);
            DatabaseManager::instance().database().open();
            DatabaseManager::instance().reapplyKey();
            statusBar()->showMessage(tr("Restore cancelled — original data preserved."), 5000);
            return;
        }
        // Unlock succeeded — keychain has been updated with the restored DB's key
    }

    // Restore successful — clean up and refresh UI
    QFile::remove(tmpPath);
    AuditLog::record(AuditLog::RESTORE, "database", 0,
        QString("Database restored from: %1").arg(src));
    m_currentEmployeeId = -1;
    m_statusLabel->setText(tr("Select an employee to get started"));
    m_employeePanel->refreshList();
    m_attendanceTab->setEmployee(Employee{}, QString());
    m_salaryTab->setEmployee(-1, QString());
    updatePinMenu();
    updateStatusBar();
    updateBypassWarning();
    QMessageBox::information(this, tr("Restore Successful"),
        tr("Database restored successfully.\nAll data has been reloaded."));
    statusBar()->showMessage(tr("Database restored from: %1").arg(src), 5000);
}

// ── Import Attendance ─────────────────────────────────────────────────────

void MainWindow::onImportAttendance() {
    const auto result = ImportHelper::importAttendance(this);

    // Nothing happened — user cancelled the file picker or dialog
    if (result.imported == 0 && result.skipped == 0 &&
        result.failed  == 0 && result.created == 0)
        return;

    // ── Refresh UI ────────────────────────────────────────────────────────
    // Refresh employee panel if any employees were created
    if (result.created > 0)
        m_employeePanel->refreshList();

    // Refresh attendance/salary views if records were imported
    if (result.imported > 0) {
        m_employeePanel->refreshList();   // also catches new employees with records
        if (m_currentEmployeeId > 0) {
            m_attendanceTab->refresh();
            m_salaryTab->refresh();
        }
    }

    // ── Build summary message ─────────────────────────────────────────────
    QString summary;
    summary += tr("Import complete.\n\n");
    summary += tr("✓ Imported:  %1 record(s)\n").arg(result.imported);
    if (result.created > 0)
        summary += tr("✚ Created:   %1 employee(s)\n").arg(result.created);
    if (result.skipped > 0)
        summary += tr("⚠ Skipped:   %1 record(s) (overlap)\n").arg(result.skipped);
    if (result.failed > 0)
        summary += tr("✗ Failed:    %1 record(s)\n").arg(result.failed);

    // Notices — wage mismatch decisions, auto-created employees
    if (!result.notices.isEmpty()) {
        summary += tr("\nNotes:\n");
        for (const auto& n : result.notices)
            summary += "• " + n + "\n";
    }

    // Warnings — overlap/failure detail lines (cap at 10 to avoid wall of text)
    if (!result.warnings.isEmpty()) {
        summary += tr("\nDetails:\n");
        const int shown = qMin(result.warnings.size(), 10);
        for (int i = 0; i < shown; ++i)
            summary += "• " + result.warnings[i] + "\n";
        if (result.warnings.size() > 10)
            summary += tr("• ... and %1 more.\n").arg(result.warnings.size() - 10);
    }

    if (result.failed > 0 || result.skipped > 0)
        QMessageBox::warning(this, tr("Import Attendance"), summary);
    else
        QMessageBox::information(this, tr("Import Attendance"), summary);

    statusBar()->showMessage(
        tr("Import: %1 imported, %2 created, %3 skipped, %4 failed.")
            .arg(result.imported).arg(result.created)
            .arg(result.skipped).arg(result.failed), 8000);
}

void MainWindow::onExportAll() {
    const bool hideW = LockPolicy::isLocked(LockPolicy::Feature::HideWages)
                       && !SessionManager::isUnlocked();
    ExportHelper::exportAll(this, !hideW);
}

// ── Employee / Attendance callbacks ──────────────────────────────────────

void MainWindow::onEmployeeSelected(int employeeId) {
    m_currentEmployeeId = employeeId;
    if (employeeId <= 0) {
        m_statusLabel->setText(tr("Select an employee to get started"));
        m_attendanceTab->setEmployee(Employee{}, QString());
        m_salaryTab->setEmployee(-1, QString());
        return;
    }
    auto emp = EmployeeRepository::instance().getEmployee(employeeId);
    if (!emp) return;
    const bool hideW = LockPolicy::isLocked(LockPolicy::Feature::HideWages) && !SessionManager::isUnlocked();

    // Status bar: show wage info appropriate to pay type
    if (emp->isMonthly()) {
        m_statusLabel->setText(
            tr("Employee: %1   |   Monthly: %2   |   Phone: %3")
                .arg(emp->name)
                .arg(hideW ? QStringLiteral("--") : CurrencyManager::format(emp->monthlySalary))
                .arg(hideW ? QStringLiteral("--") : (emp->phone.isEmpty() ? tr("--") : emp->phone)));
    } else {
        m_statusLabel->setText(
            tr("Employee: %1   |   Wage: %2 %3/hr   |   Phone: %4")
                .arg(emp->name)
                .arg(hideW ? QStringLiteral("--") : QString::number(emp->hourlyWage, 'f', 2))
                .arg(hideW ? QStringLiteral("") : CurrencyManager::symbol())
                .arg(hideW ? QStringLiteral("--") : (emp->phone.isEmpty() ? tr("--") : emp->phone)));
    }

    m_attendanceTab->setEmployee(*emp, emp->name);
    m_attendanceTab->setMonth(m_currentYear, m_currentMonth);
    m_salaryTab->setEmployee(employeeId, emp->name);
    m_salaryTab->setMonth(m_currentYear, m_currentMonth);
}

void MainWindow::onAttendanceChanged() { m_salaryTab->refresh(); }

void MainWindow::onMonthChanged(int year, int month) {
    m_currentYear  = year;
    m_currentMonth = month;
    m_salaryTab->setMonth(year, month);
}

// ── Developer slots ───────────────────────────────────────────────────────

void MainWindow::onDevAppInfo() {
    const QString dataDir  = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    const QString dbPath   = m_dbPath;
    const QString lockPath = dbPath + ".lock";
    const QString backupDir = AutoBackupManager::backupFolder();
    const QFileInfoList backups = QDir(backupDir).entryInfoList(
        {"auto_*.db"}, QDir::Files, QDir::Time);
    const QFileInfo dbInfo(dbPath);
    const QString dbSize = dbInfo.exists()
        ? QString("%1 KB").arg(dbInfo.size() / 1024) : "file not found";
    const bool isOverride = (dbPath != dataDir + "/attendance.db");

    // ── Credential version ────────────────────────────────────────────────
    const int credVer = PinManager::credentialVersion();
    const QString credVerStr = PinManager::isPinSet()
        ? QString::number(credVer) : "n/a (no PIN set)";

    // ── DB encryption status ──────────────────────────────────────────────
    QString dbEncryption;
    if (!PinManager::getBypassTimestamp().isEmpty())
        dbEncryption = "fallback key (PIN removed via bypass)";
    else if (PinManager::isPinSet())
        dbEncryption = "user key (PIN set)";
    else
        dbEncryption = "fallback key (no PIN)";

    const QString info = QString(
        "=== Rawatib Dev Info ===\n\n"
        "App Version:      %1\n"
        "Qt Version:       %2\n\n"
        "Language (saved): %3\n"
        "Layout direction: %4\n\n"
        "DB path:          %5\n"
        "DB path override: %6\n"
        "DB size:          %7\n"
        "DB encryption:    %8\n"
        "App data dir:     %9\n"
        "Lock file:        %10\n\n"
        "Auto-backup dir:  %11\n"
        "Auto-backups:     %12 file(s)\n\n"
        "Admin password:   %13\n"
        "Credential ver:   %14\n"
        "Recovery file:    %15\n"
        "Pass bypassed:    %16\n"
    )
    .arg(QCoreApplication::applicationVersion())
    .arg(qVersion())
    .arg(SettingsManager::getLanguage())
    .arg(QGuiApplication::layoutDirection() == Qt::RightToLeft ? "RTL" : "LTR")
    .arg(dbPath)
    .arg(isOverride ? "YES (--db-path)" : "no")
    .arg(dbSize)
    .arg(dbEncryption)
    .arg(dataDir)
    .arg(lockPath)
    .arg(backupDir)
    .arg(backups.size())
    .arg(PinManager::isPinSet() ? "yes" : "no")
    .arg(credVerStr)
    .arg(PinManager::hasRecoveryFile() ? "generated" : "not generated")
    .arg(PinManager::getBypassTimestamp().isEmpty()
         ? "no" : QString("yes (at %1)").arg(PinManager::getBypassTimestamp()));

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("App Info"));
    dlg->setMinimumSize(520, 360);
    auto* layout = new QVBoxLayout(dlg);
    auto* textEdit = new QTextEdit(dlg);
    textEdit->setReadOnly(true);
    textEdit->setFont(QFont("Courier New", 9));
    textEdit->setPlainText(info);
    layout->addWidget(textEdit);
    auto* btnBox  = new QDialogButtonBox(dlg);
    auto* copyBtn = btnBox->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
    auto* closeBtn = btnBox->addButton(QDialogButtonBox::Close);
    layout->addWidget(btnBox);
    connect(copyBtn,  &QPushButton::clicked, this, [info]() {
        QGuiApplication::clipboard()->setText(info); });
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onDevResetSettings() {
    // ── Build custom dialog ───────────────────────────────────────────────
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Reset Settings"));
    dlg.setMinimumWidth(420);

    auto* layout  = new QVBoxLayout(&dlg);
    layout->setSpacing(12);

    auto* msgLabel = new QLabel(
        tr("This will clear all saved settings (language, session timeout, "
           "payroll rules toggle) and restart Rawatib."), &dlg);
    msgLabel->setWordWrap(true);
    layout->addWidget(msgLabel);

    // Checkbox — unchecked by default
    auto* deleteDbCheck = new QCheckBox(
        tr("Also delete the database (removes all employees, attendance records, and settings)"),
        &dlg);
    deleteDbCheck->setChecked(false);
    layout->addWidget(deleteDbCheck);

    // Warning label — only visible when checkbox is ticked
    auto* dbWarningLabel = new QLabel(
        tr("⚠  This is irreversible. All data will be permanently lost."), &dlg);
    QPalette warnPal = dbWarningLabel->palette();
    warnPal.setColor(QPalette::WindowText,
        QColor(ThemeHelper::isDark() ? "#ef9a9a" : "#c0392b"));
    dbWarningLabel->setPalette(warnPal);
    dbWarningLabel->setWordWrap(true);
    dbWarningLabel->setVisible(false);
    layout->addWidget(dbWarningLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    // Show/hide warning as checkbox changes
    connect(deleteDbCheck, &QCheckBox::toggled,
            dbWarningLabel, &QLabel::setVisible);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const bool deleteDb = deleteDbCheck->isChecked();

    // ── Second confirmation if DB deletion requested ──────────────────────
    if (deleteDb) {
        const auto confirm = QMessageBox::warning(this,
            tr("Delete Database — Are You Sure?"),
            tr("All employees, attendance records, payroll rules, "
               "audit log, and settings stored in the database will be "
               "permanently deleted.\n\n"
               "This cannot be undone. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm != QMessageBox::Yes) return;
    }

    // ── Perform reset ─────────────────────────────────────────────────────
    if (deleteDb) {
        // Close DB connection cleanly before touching the file —
        // on Windows an open file handle prevents deletion.
        auto& dbManager = DatabaseManager::instance();
        if (dbManager.isOpen()) {
            dbManager.database().close();
            QSqlDatabase::removeDatabase("main_connection");
        }

        // Remove keychain entry — without this, next launch tries to open
        // a non-existent DB with a stale cipher key and fails to initialize.
        PinManager::deleteKey();

        // Delete DB file and its lock file
        QFile::remove(m_dbPath);
        QFile::remove(m_dbPath + ".lock");
    }

    // Clear QSettings regardless
    SettingsManager::resetAll();

    // Restart — pass --dev-mode so the Developer menu is still available
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            {"--dev-mode"});
    QApplication::quit();
}

// ── Update checker ─────────────────────────────────────────────────────────

// Called by the silent background check when a newer version is found.
// Badges the Help menu title and changes the action text — no popup.
void MainWindow::onUpdateFound(const QString& ver, const QString& url) {
    m_pendingUpdateVer = ver;
    m_pendingUpdateUrl = url;

    // Badge the Help menu title so the user notices without a popup
    m_helpMenu->setTitle(tr("&Help  ●"));

    // Change the action text — visible as soon as they open the menu
    m_checkUpdateAction->setText(
        tr("Update Available — v%1  \xF0\x9F\x86\x95").arg(ver));
    m_checkUpdateAction->setToolTip(
        tr("Rawatib %1 is available. Click to see what's new.").arg(ver));
}

// Triggered by Help → Check for Updates (manual).
// If a pending update is already known, shows the dialog immediately
// without a second network request. Otherwise fires a fresh check.
void MainWindow::onCheckForUpdates() {
    if (!m_pendingUpdateVer.isEmpty()) {
        // Already know about an update — go straight to dialog
        showUpdateDialog(m_pendingUpdateVer, m_pendingUpdateUrl, QString());
        return;
    }

    // Fresh check — show a brief "Checking..." state
    m_checkUpdateAction->setText(tr("Checking..."));
    m_checkUpdateAction->setEnabled(false);

    auto* checker = new UpdateChecker(this);

    connect(checker, &UpdateChecker::updateAvailable,
            this, [this](const QString& ver,
                         const QString& url,
                         const QString& notes) {
        m_checkUpdateAction->setEnabled(true);
        showUpdateDialog(ver, url, notes);
    });

    connect(checker, &UpdateChecker::alreadyUpToDate, this, [this]() {
        m_checkUpdateAction->setText(tr("Check for Updates  \xE2\x9C\x93"));
        m_checkUpdateAction->setEnabled(true);
        // Reset back to plain text after 4 seconds
        QTimer::singleShot(4000, this, [this]() {
            if (m_pendingUpdateVer.isEmpty())
                m_checkUpdateAction->setText(tr("Check for Updates"));
        });
    });

    connect(checker, &UpdateChecker::checkFailed,
            this, [this](const QString& err) {
        m_checkUpdateAction->setText(tr("Check for Updates"));
        m_checkUpdateAction->setEnabled(true);
        QMessageBox::warning(this, tr("Update Check Failed"),
            tr("Could not check for updates.\n%1").arg(err));
    });

    checker->check(/*silent=*/false);
}

// Shows the update-available dialog and resets all badge state.
// Called both from onCheckForUpdates() (manual) and onUpdateFound() path.
void MainWindow::showUpdateDialog(const QString& ver,
                                   const QString& url,
                                   const QString& notes) {
    // ── Reset badge and action text ───────────────────────────────────────
    m_helpMenu->setTitle(tr("&Help"));
    m_checkUpdateAction->setText(tr("Check for Updates"));
    m_checkUpdateAction->setToolTip(QString());
    m_pendingUpdateVer.clear();
    m_pendingUpdateUrl.clear();

    // Remember this version so the silent check won't re-badge it
    SettingsManager::setValue("lastDismissedUpdate", ver);

    // ── Show dialog ───────────────────────────────────────────────────────
    QMessageBox dlg(this);
    dlg.setWindowTitle(tr("Update Available"));
    dlg.setIcon(QMessageBox::Information);
    dlg.setText(
        tr("<b>Rawatib %1 is available.</b><br>"
           "You are running version %2.<br><br>"
           "Download the latest version from the releases page.")
        .arg(ver, QCoreApplication::applicationVersion()));

    // Release notes go in the expandable "Details" section — keeps the
    // dialog compact while still giving users access to the full changelog.
    if (!notes.isEmpty())
        dlg.setDetailedText(notes);

    auto* openBtn = dlg.addButton(tr("Open Download Page"),
                                   QMessageBox::AcceptRole);
    dlg.addButton(tr("Later"), QMessageBox::RejectRole);
    dlg.exec();

    if (dlg.clickedButton() == openBtn) {
        // Validate the URL at the point of opening — defence in depth.
        // UpdateChecker already validated it, but the URL passes through
        // m_pendingUpdateUrl (stored in memory) before reaching here.
        // Accept only HTTPS github.com URLs; fall back to releases page otherwise.
        const QUrl releaseUrl(url);
        const QString host = releaseUrl.host().toLower();
        const bool isSafe  = releaseUrl.isValid()
                          && releaseUrl.scheme() == QLatin1String("https")
                          && (host == QLatin1String("github.com")
                              || host.endsWith(QLatin1String(".github.com")));

        QDesktopServices::openUrl(
            isSafe ? releaseUrl
                   : QUrl(QStringLiteral("https://github.com/FakyLab/Rawatib/releases")));
    }
}
