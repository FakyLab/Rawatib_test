#include "ui/AttendanceTab.h"
#include "ui/dialogs/AttendanceDialog.h"
#include "ui/dialogs/PayMonthDialog.h"
#include "ui/dialogs/PinDialog.h"
#include "repositories/AttendanceRepository.h"
#include "repositories/EmployeeRepository.h"
#include "utils/PrintHelper.h"
#include "utils/PinManager.h"
#include "utils/CurrencyManager.h"
#include "utils/LockPolicy.h"
#include "utils/EmployeePinManager.h"
#include "utils/SessionManager.h"
#include "utils/ThemeHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QDate>
#include <QTime>
#include <QFrame>
#include <QMap>

static inline QLocale appLocale() {
    return QGuiApplication::layoutDirection() == Qt::RightToLeft
               ? QLocale(QLocale::Arabic) : QLocale::c();
}

// ── UserRole constants ────────────────────────────────────────────────────
static constexpr int kRoleId        = Qt::UserRole;       // int: record id (session) or 0 (day)
static constexpr int kRoleIsSession = Qt::UserRole + 1;   // bool: true = session leaf item

AttendanceTab::AttendanceTab(QWidget* parent) : QWidget(parent) {
    m_year  = QDate::currentDate().year();
    m_month = QDate::currentDate().month();
    m_adminUnlocked = !PinManager::isPinSet();
    setupUi();
}

void AttendanceTab::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // ── Period filter ─────────────────────────────────────────────────────
    auto* filterRow = new QHBoxLayout();
    filterRow->addWidget(new QLabel(tr("Period:"), this));

    m_monthCombo = new QComboBox(this);
    for (int m = 1; m <= 12; ++m)
        m_monthCombo->addItem(appLocale().monthName(m), m);
    m_monthCombo->setCurrentIndex(m_month - 1);

    m_yearCombo = new QComboBox(this);
    const int curYear      = QDate::currentDate().year();
    const int earliestYear = AttendanceRepository::instance().getEarliestRecordYear();
    const int fromYear     = qMin(earliestYear, curYear - 1);
    for (int y = fromYear; y <= curYear + 1; ++y)
        m_yearCombo->addItem(QString::number(y), y);
    m_yearCombo->setCurrentText(QString::number(m_year));

    filterRow->addWidget(m_monthCombo);
    filterRow->addWidget(m_yearCombo);

    // ── "Show My Wages" self-view checkbox ────────────────────────────────
    // Placed after the year combo — employee control, not admin.
    // Muted style so it's clearly secondary but still clickable.
    m_selfViewCheck = new QCheckBox(tr("Show My Wages"), this);
    m_selfViewCheck->setVisible(false);
    m_selfViewCheck->setStyleSheet(
        "QCheckBox { color: palette(mid); }"
        "QCheckBox:hover { color: palette(text); }"
        "QCheckBox::indicator { width: 13px; height: 13px; }");
    m_selfViewCheck->setToolTip(
        tr("Enter your PIN to view your own wages.\n"
           "Resets automatically after 2 minutes of inactivity."));
    filterRow->addWidget(m_selfViewCheck);
    connect(m_selfViewCheck, &QCheckBox::toggled,
            this, &AttendanceTab::onSelfViewToggled);

    filterRow->addStretch();

    // ── Self-view inactivity timer ─────────────────────────────────────────
    m_selfViewTimer = new QTimer(this);
    m_selfViewTimer->setSingleShot(true);
    m_selfViewTimer->setInterval(2 * 60 * 1000);   // 2 minutes
    connect(m_selfViewTimer, &QTimer::timeout,
            this, &AttendanceTab::onSelfViewTimeout);

    m_lockBtn = new QPushButton(this);
    m_lockBtn->setFlat(true);
    m_lockBtn->setFixedSize(28, 28);
    m_lockBtn->setVisible(PinManager::isPinSet());
    filterRow->addWidget(m_lockBtn);
    refreshLockIcon();
    connect(m_lockBtn, &QPushButton::clicked,
            this, &AttendanceTab::lockIconClicked);

    mainLayout->addLayout(filterRow);

    // ── Zero-wage banner ──────────────────────────────────────────────────
    // Shown when the selected employee has no wage configured.
    m_wageBanner = new QLabel(this);
    m_wageBanner->setText(
        tr("⚠  This employee has no wage set — all salary values will be zero. "
           "Edit the employee to add a wage."));
    m_wageBanner->setWordWrap(true);
    m_wageBanner->setVisible(false);
    mainLayout->addWidget(m_wageBanner);

    // ── No expected times banner ──────────────────────────────────────────
    // Shown for monthly employees who have no expected check-in/out times set.
    m_noExpectedTimesBanner = new QLabel(this);
    m_noExpectedTimesBanner->setText(
        tr("ℹ  Expected times not set for this employee — time-based deductions are disabled. "
           "Edit the employee to configure expected check-in/out times."));
    m_noExpectedTimesBanner->setWordWrap(true);
    m_noExpectedTimesBanner->setVisible(false);
    mainLayout->addWidget(m_noExpectedTimesBanner);

    // ── Mixed deduction modes banner ──────────────────────────────────────
    // Shown when this month contains records calculated under different modes.
    m_mixedModesBanner = new QLabel(this);
    m_mixedModesBanner->setText(
        tr("⚠  This month contains records calculated under different deduction modes. "
           "Results may be inconsistent. Recalculate affected records if needed."));
    m_mixedModesBanner->setWordWrap(true);
    m_mixedModesBanner->setVisible(false);
    mainLayout->addWidget(m_mixedModesBanner);

    // Apply theme-aware stylesheets to all three banners
    applyBannerStyles();

    // ── Tree widget ───────────────────────────────────────────────────────
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(9);
    m_tree->setHeaderLabels({tr("Date"), tr("Check-In"), tr("Check-Out"),
                             tr("Hours"),
                             tr("Base Rate"), tr("Deduction"), tr("Net Day"),
                             tr("Late/Early"), tr("Status")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &AttendanceTab::onContextMenu);
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &AttendanceTab::onItemSelectionChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
        // Double-click on a session row = edit it
        if (item && item->data(0, kRoleIsSession).toBool())
            onEditRecord();
    });

    mainLayout->addWidget(m_tree, 1);

    // ── Button row ────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);

    m_addBtn    = new QPushButton(tr("Add Record"),    this);
    m_editBtn   = new QPushButton(tr("Edit Record"),   this);
    m_deleteBtn = new QPushButton(tr("Delete Record"), this);
    m_editBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_deleteBtn);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    btnRow->addWidget(sep);

    m_checkInBtn  = new QPushButton(tr("Check In"),  this);
    m_checkOutBtn = new QPushButton(tr("Check Out"), this);
    m_checkInBtn->setEnabled(false);
    m_checkOutBtn->setEnabled(false);
    btnRow->addWidget(m_checkInBtn);
    btnRow->addWidget(m_checkOutBtn);

    btnRow->addStretch();

    m_markPaidBtn      = new QPushButton(tr("Mark Paid"),        this);
    m_markMonthPaidBtn = new QPushButton(tr("Pay Entire Month"), this);
    m_markPaidBtn->setEnabled(false);
    m_markMonthPaidBtn->setEnabled(false);
    btnRow->addWidget(m_markPaidBtn);
    btnRow->addWidget(m_markMonthPaidBtn);
    mainLayout->addLayout(btnRow);

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_addBtn,    &QPushButton::clicked, this, &AttendanceTab::onAddRecord);
    connect(m_editBtn,   &QPushButton::clicked, this, &AttendanceTab::onEditRecord);
    connect(m_deleteBtn, &QPushButton::clicked, this, &AttendanceTab::onDeleteRecord);
    connect(m_markPaidBtn, &QPushButton::clicked, this, &AttendanceTab::onMarkPaid);
    connect(m_checkInBtn,  &QPushButton::clicked, this, &AttendanceTab::onCheckIn);
    connect(m_checkOutBtn, &QPushButton::clicked, this, &AttendanceTab::onCheckOut);

    connect(m_markMonthPaidBtn, &QPushButton::clicked, this, [this]() {
        if (!guardMarkPaid()) return;
        if (m_employeeId <= 0) return;
        auto summary = AttendanceRepository::instance()
                           .getMonthlySummary(m_employeeId, m_year, m_month);
        if (summary.unpaidAmount <= 0) return;

        PayMonthDialog dlg(m_employeeName, m_month, m_year, this);
        if (dlg.exec() != QDialog::Accepted) return;

        const double previouslyPaid = summary.paidAmount;
        const double amountPaidNow  = summary.unpaidAmount;
        const double totalSalary    = summary.totalSalary;

        AttendanceRepository::instance().markMonthPaid(m_employeeId, m_year, m_month);
        loadRecords();
        refreshPayBtn();
        refreshCheckButtons();
        emit attendanceChanged();

        if (dlg.printRequested()) {
            PrintHelper::printPaymentSlip(m_employeeName, m_month, m_year,
                                          totalSalary, previouslyPaid,
                                          amountPaidNow, this);
        }
    });

    connect(m_monthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AttendanceTab::onMonthChanged);
    connect(m_yearCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AttendanceTab::onMonthChanged);

    // Re-apply banner stylesheets when the system theme changes at runtime
    ThemeHelper::onThemeChanged(this, [this]() { applyBannerStyles(); });
}

// ── Banner styles ─────────────────────────────────────────────────────────
// Called once from setupUi() and again via ThemeHelper::onThemeChanged()
// whenever the system switches between light and dark mode at runtime.

void AttendanceTab::applyBannerStyles() {
    const bool dark = ThemeHelper::isDark();

    const QString amberStyle = dark
        ? "QLabel { color: #ffcc80; background-color: #3d2800; "
          "border: 1px solid #8a6200; border-radius: 3px; padding: 5px 8px; }"
        : "QLabel { color: #7a4f00; background-color: #fff8e1; "
          "border: 1px solid #f0c040; border-radius: 3px; padding: 5px 8px; }";

    const QString blueStyle = dark
        ? "QLabel { color: #90caf9; background-color: #0d2a3d; "
          "border: 1px solid #1565c0; border-radius: 3px; padding: 5px 8px; }"
        : "QLabel { color: #1a5276; background-color: #d6eaf8; "
          "border: 1px solid #85c1e9; border-radius: 3px; padding: 5px 8px; }";

    m_wageBanner->setStyleSheet(amberStyle);
    m_noExpectedTimesBanner->setStyleSheet(blueStyle);
    m_mixedModesBanner->setStyleSheet(amberStyle);
}

// ── Lock state ────────────────────────────────────────────────────────────

void AttendanceTab::onLockChanged(bool unlocked) {
    m_adminUnlocked = unlocked;
    refreshLockIcon();
    m_lockBtn->setVisible(PinManager::isPinSet());

    // When admin unlocks, hide the self-view checkbox entirely
    // When admin locks, reset self-view and re-evaluate visibility
    if (unlocked) {
        m_selfViewActive = false;
        m_selfViewTimer->stop();
        if (m_selfViewCheck) {
            m_selfViewCheck->blockSignals(true);
            m_selfViewCheck->setChecked(false);
            m_selfViewCheck->blockSignals(false);
            m_selfViewCheck->setVisible(false);
        }
    } else {
        refreshSelfViewCheckbox();
    }

    // Refresh wage banner and tree values
    m_wageBanner->setVisible(m_employeeId > 0 && wagesVisible() && (
        (m_employee.isMonthly() && m_employee.monthlySalary == 0.0) ||
        (!m_employee.isMonthly() && m_employee.hourlyWage == 0.0)));
    loadRecords();
}

void AttendanceTab::refreshLockIcon() {
    if (!m_lockBtn) return;
    m_lockBtn->setText(m_adminUnlocked ? "🔓" : "🔒");
    m_lockBtn->setToolTip(m_adminUnlocked
        ? tr("Lock admin access")
        : tr("Unlock admin access"));
}

// ── Wage visibility ───────────────────────────────────────────────────────

bool AttendanceTab::wagesVisible() const {
    if (!LockPolicy::isLocked(LockPolicy::Feature::HideWages)) return true;
    if (SessionManager::isUnlocked()) return true;
    return m_selfViewActive;
}

void AttendanceTab::refreshSelfViewCheckbox() {
    if (!m_selfViewCheck) return;
    // Visible only when: HideWages locked + admin locked + employee has a PIN
    const bool shouldShow = LockPolicy::isLocked(LockPolicy::Feature::HideWages)
                         && !m_adminUnlocked
                         && m_employeeId > 0
                         && [this]() {
                                auto emp = EmployeeRepository::instance().getEmployee(m_employeeId);
                                return emp && emp->hasPinSet();
                            }();
    m_selfViewCheck->setVisible(shouldShow);
    if (!shouldShow) {
        m_selfViewActive = false;
        m_selfViewTimer->stop();
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(false);
        m_selfViewCheck->blockSignals(false);
    }
}

void AttendanceTab::onSelfViewToggled(bool checked) {
    if (checked) {
        // Ask employee for their PIN
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Employee PIN"));
        dlg.setMinimumWidth(300);
        auto* layout = new QVBoxLayout(&dlg);
        layout->setSpacing(10);
        layout->setContentsMargins(16, 16, 16, 12);

        auto* prompt = new QLabel(tr("Enter your PIN to view your wages."), &dlg);
        prompt->setWordWrap(true);
        layout->addWidget(prompt);

        auto* pinEdit = new QLineEdit(&dlg);
        pinEdit->setEchoMode(QLineEdit::Password);
        pinEdit->setMaxLength(12);
        pinEdit->setPlaceholderText(tr("6–12 digits"));
        pinEdit->setValidator(new QRegularExpressionValidator(
            QRegularExpression("\\d{0,12}"), pinEdit));
        pinEdit->setLayoutDirection(Qt::LeftToRight);
        layout->addWidget(pinEdit);

        auto* errorLabel = new QLabel(&dlg);
        errorLabel->setStyleSheet(ThemeHelper::isDark() ? "color: #ef9a9a;" : "color: #c0392b;");
        errorLabel->hide();
        layout->addWidget(errorLabel);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        bool verified = false;
        connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
            if (EmployeePinManager::verifyPin(m_employeeId, pinEdit->text())) {
                verified = true;
                dlg.accept();
            } else {
                errorLabel->setText(tr("Incorrect PIN. Try again."));
                errorLabel->show();
                pinEdit->clear();
                pinEdit->setFocus();
                dlg.adjustSize();
            }
        });

        dlg.exec();

        if (verified) {
            m_selfViewActive = true;
            m_selfViewTimer->start();
            emit selfViewActivated(true);
        } else {
            // User cancelled or wrong PIN — uncheck without triggering signal
            m_selfViewCheck->blockSignals(true);
            m_selfViewCheck->setChecked(false);
            m_selfViewCheck->blockSignals(false);
            m_selfViewActive = false;
        }
    } else {
        // Manually unchecked
        m_selfViewActive = false;
        m_selfViewTimer->stop();
        emit selfViewActivated(false);
    }

    // Refresh display
    m_wageBanner->setVisible(m_employeeId > 0 && wagesVisible() && (
        (m_employee.isMonthly() && m_employee.monthlySalary == 0.0) ||
        (!m_employee.isMonthly() && m_employee.hourlyWage == 0.0)));
    loadRecords();
}

void AttendanceTab::onSelfViewTimeout() {
    // 2-minute inactivity — reset self-view silently
    m_selfViewActive = false;
    if (m_selfViewCheck) {
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(false);
        m_selfViewCheck->blockSignals(false);
    }
    emit selfViewActivated(false);
    m_wageBanner->setVisible(m_employeeId > 0 && wagesVisible() && (
        (m_employee.isMonthly() && m_employee.monthlySalary == 0.0) ||
        (!m_employee.isMonthly() && m_employee.hourlyWage == 0.0)));
    loadRecords();
}

void AttendanceTab::setSelfViewActive(bool active) {
    // Called by MainWindow when SalaryTab's self-view changes
    m_selfViewActive = active;
    m_selfViewTimer->stop();
    if (m_selfViewCheck) {
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(active);
        m_selfViewCheck->blockSignals(false);
    }
    m_wageBanner->setVisible(m_employeeId > 0 && wagesVisible() && (
        (m_employee.isMonthly() && m_employee.monthlySalary == 0.0) ||
        (!m_employee.isMonthly() && m_employee.hourlyWage == 0.0)));
    loadRecords();
}

bool AttendanceTab::guardAdmin(LockPolicy::Feature feature) {
    if (!LockPolicy::isLocked(feature)) return true;
    if (SessionManager::isUnlocked()) return true;
    if (PinDialog::requestUnlock(this)) {
        SessionManager::setUnlocked(true);
        emit adminUnlocked();
        return true;
    }
    return false;
}

// ── guardMarkPaid ─────────────────────────────────────────────────────────
// Three-state gate for the Mark Paid action:
//   1. MarkPaid unlocked  → always allow
//   2. Admin unlocked     → always allow
//   3. Employee PIN feature on + this employee has a PIN
//                         → ask for employee PIN, verify against employeeId
//   4. Otherwise          → inline admin unlock at point of action
bool AttendanceTab::guardMarkPaid() {
    if (!LockPolicy::isLocked(LockPolicy::Feature::MarkPaid)) return true;
    if (SessionManager::isUnlocked()) return true;

    if (EmployeePinManager::isFeatureEnabled() && m_employeeId > 0) {
        // Check if this employee has a PIN set
        auto emp = EmployeeRepository::instance().getEmployee(m_employeeId);
        if (emp && emp->hasPinSet()) {
            // Show employee PIN dialog
            QDialog dlg(this);
            dlg.setWindowTitle(tr("Employee PIN"));
            dlg.setMinimumWidth(300);
            auto* layout = new QVBoxLayout(&dlg);
            layout->setSpacing(10);
            layout->setContentsMargins(16, 16, 16, 12);

            auto* prompt = new QLabel(
                tr("Enter your PIN to mark your records as paid."), &dlg);
            prompt->setWordWrap(true);
            layout->addWidget(prompt);

            auto* pinEdit = new QLineEdit(&dlg);
            pinEdit->setEchoMode(QLineEdit::Password);
            pinEdit->setMaxLength(12);
            pinEdit->setPlaceholderText(tr("6–12 digits"));
            pinEdit->setValidator(new QRegularExpressionValidator(
                QRegularExpression("\\d{0,12}"), pinEdit));
            pinEdit->setLayoutDirection(Qt::LeftToRight);
            layout->addWidget(pinEdit);

            auto* errorLabel = new QLabel(&dlg);
            errorLabel->setStyleSheet(ThemeHelper::isDark() ? "color: #ef9a9a;" : "color: #c0392b;");
            errorLabel->hide();
            layout->addWidget(errorLabel);

            auto* buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            bool verified = false;
            connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
                if (EmployeePinManager::verifyPin(m_employeeId, pinEdit->text())) {
                    verified = true;
                    dlg.accept();
                } else {
                    errorLabel->setText(tr("Incorrect PIN. Try again."));
                    errorLabel->show();
                    pinEdit->clear();
                    pinEdit->setFocus();
                    dlg.adjustSize();
                }
            });

            dlg.exec();
            return verified;
        }
    }

    // Fallback — inline admin unlock
    if (PinDialog::requestUnlock(this)) {
        SessionManager::setUnlocked(true);
        emit adminUnlocked();
        return true;
    }
    return false;
}

// ── guardKioskPin ─────────────────────────────────────────────────────────
// Three-state gate for Check-in / Check-out:
//   1. CheckIn/Out unlocked  → always allow (PIN feature irrelevant)
//   2. Admin unlocked        → always allow
//   3. Kiosk PIN feature on + employee has PIN
//                            → ask for employee PIN
//   4. Otherwise             → inline admin unlock at point of action
bool AttendanceTab::guardKioskPin(LockPolicy::Feature feature) {
    if (!LockPolicy::isLocked(feature)) return true;
    if (SessionManager::isUnlocked()) return true;

    if (EmployeePinManager::isKioskPinEnabled() && m_employeeId > 0) {
        auto emp = EmployeeRepository::instance().getEmployee(m_employeeId);
        if (emp && emp->hasPinSet()) {
            const bool isCheckIn = (feature == LockPolicy::Feature::CheckIn);
            QDialog dlg(this);
            dlg.setWindowTitle(tr("Employee PIN"));
            dlg.setMinimumWidth(300);
            auto* layout = new QVBoxLayout(&dlg);
            layout->setSpacing(10);
            layout->setContentsMargins(16, 16, 16, 12);

            auto* prompt = new QLabel(
                isCheckIn
                    ? tr("Enter your PIN to check in.")
                    : tr("Enter your PIN to check out."), &dlg);
            prompt->setWordWrap(true);
            layout->addWidget(prompt);

            auto* pinEdit = new QLineEdit(&dlg);
            pinEdit->setEchoMode(QLineEdit::Password);
            pinEdit->setMaxLength(12);
            pinEdit->setPlaceholderText(tr("6–12 digits"));
            pinEdit->setValidator(new QRegularExpressionValidator(
                QRegularExpression("\\d{0,12}"), pinEdit));
            pinEdit->setLayoutDirection(Qt::LeftToRight);
            layout->addWidget(pinEdit);

            auto* errorLabel = new QLabel(&dlg);
            errorLabel->setStyleSheet(ThemeHelper::isDark() ? "color: #ef9a9a;" : "color: #c0392b;");
            errorLabel->hide();
            layout->addWidget(errorLabel);

            auto* buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

            bool verified = false;
            connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
                if (EmployeePinManager::verifyPin(m_employeeId, pinEdit->text())) {
                    verified = true;
                    dlg.accept();
                } else {
                    errorLabel->setText(tr("Incorrect PIN. Try again."));
                    errorLabel->show();
                    pinEdit->clear();
                    pinEdit->setFocus();
                    dlg.adjustSize();
                }
            });

            dlg.exec();
            return verified;
        }
    }

    // Fallback — inline admin unlock
    if (PinDialog::requestUnlock(this)) {
        SessionManager::setUnlocked(true);
        emit adminUnlocked();
        return true;
    }
    return false;
}

void AttendanceTab::onCheckIn() {
    if (!guardKioskPin(LockPolicy::Feature::CheckIn)) return;
    if (m_employeeId <= 0) return;
    const QDate today = QDate::currentDate();
    const QTime now   = QTime::currentTime();

    if (!AttendanceRepository::instance().checkIn(m_employeeId, today, now)) {
        QMessageBox::warning(this, tr("Check-In Failed"),
            AttendanceRepository::instance().lastError());
        return;
    }
    jumpToCurrentMonth();
    emit attendanceChanged();
}

void AttendanceTab::onCheckOut() {
    if (!guardKioskPin(LockPolicy::Feature::CheckOut)) return;
    if (m_employeeId <= 0) return;
    const QDate today = QDate::currentDate();
    const QTime now   = QTime::currentTime();

    auto rec = AttendanceRepository::instance().getTodayRecord(m_employeeId, today);
    if (rec.id == 0 || !rec.isOpen()) return;

    // Re-fetch employee to ensure wage/schedule config is current at checkout time
    auto freshEmp = EmployeeRepository::instance().getEmployee(m_employeeId);
    if (!freshEmp) return;

    if (!AttendanceRepository::instance().checkOut(rec.id, now, *freshEmp)) {
        QMessageBox::warning(this, tr("Check-Out Failed"),
            AttendanceRepository::instance().lastError());
        return;
    }
    jumpToCurrentMonth();
    emit attendanceChanged();
}

// ── refreshCheckButtons ───────────────────────────────────────────────────
// Allow a new check-in after a completed session — just guard against
// checking in while an open record already exists.

void AttendanceTab::refreshCheckButtons() {
    if (m_employeeId <= 0) {
        m_checkInBtn->setEnabled(false);
        m_checkOutBtn->setEnabled(false);
        return;
    }

    auto openRec = AttendanceRepository::instance()
                       .getTodayRecord(m_employeeId, QDate::currentDate());

    if (openRec.id == 0) {
        // No open record — can check in (even if completed records exist today)
        m_checkInBtn->setEnabled(true);
        m_checkOutBtn->setEnabled(false);
    } else {
        // Open record exists — can only check out
        m_checkInBtn->setEnabled(false);
        m_checkOutBtn->setEnabled(true);
    }
}

// ── Jump ──────────────────────────────────────────────────────────────────

void AttendanceTab::jumpToCurrentMonth() {
    const int y = QDate::currentDate().year();
    const int m = QDate::currentDate().month();
    m_year  = y;
    m_month = m;
    m_monthCombo->blockSignals(true);
    m_yearCombo->blockSignals(true);
    m_yearCombo->setCurrentText(QString::number(y));
    m_monthCombo->setCurrentIndex(m - 1);
    m_monthCombo->blockSignals(false);
    m_yearCombo->blockSignals(false);
    loadRecords();
    refreshPayBtn();
    refreshCheckButtons();
    emit monthChanged(y, m);
}

// ── Public interface ──────────────────────────────────────────────────────

void AttendanceTab::setEmployee(const Employee& employee, const QString& employeeName) {
    m_employee     = employee;
    m_employeeId   = employee.id;
    m_employeeName = employeeName;
    m_addBtn->setEnabled(employee.id > 0);

    // Reset self-view when switching employees
    m_selfViewActive = false;
    m_selfViewTimer->stop();
    if (m_selfViewCheck) {
        m_selfViewCheck->blockSignals(true);
        m_selfViewCheck->setChecked(false);
        m_selfViewCheck->blockSignals(false);
    }

    // Show zero-wage banner: hourly with no wage, or monthly with no salary
    const bool zeroWage = employee.id > 0 && wagesVisible() && (
        (employee.isMonthly() && employee.monthlySalary == 0.0) ||
        (!employee.isMonthly() && employee.hourlyWage == 0.0));
    m_wageBanner->setVisible(zeroWage);

    // Show no-expected-times banner: monthly employee with no times configured
    const bool noExpectedTimes = employee.id > 0 && employee.isMonthly() &&
        !employee.expectedCheckin.isValid() && !employee.expectedCheckout.isValid();
    m_noExpectedTimesBanner->setVisible(noExpectedTimes);

    // Show/hide columns based on pay type:
    // Monthly: show Base Rate(4), Deduction(5), Net Day(6), Late/Early(7)
    // Hourly:  hide Base Rate(4), Deduction(5), Late/Early(7) — show only Net Day(6)
    const bool monthly = employee.isMonthly();
    m_tree->setColumnHidden(4, !monthly);
    m_tree->setColumnHidden(5, !monthly);
    m_tree->setColumnHidden(7, !monthly);

    refreshSelfViewCheckbox();
    loadRecords();
    refreshPayBtn();
    refreshCheckButtons();
}

void AttendanceTab::setMonth(int year, int month) {
    m_year = year; m_month = month;
    m_yearCombo->setCurrentText(QString::number(year));
    m_monthCombo->setCurrentIndex(month - 1);
    loadRecords();
}

void AttendanceTab::refresh() {
    const bool monthly = m_employee.isMonthly();
    const QString sym  = CurrencyManager::symbol();
    m_tree->headerItem()->setText(4, tr("Base Rate") + " (" + sym + ")");
    m_tree->headerItem()->setText(5, tr("Deduction") + " (" + sym + ")");
    m_tree->headerItem()->setText(6, tr("Net Day")   + " (" + sym + ")");
    m_tree->setColumnHidden(4, !monthly);
    m_tree->setColumnHidden(5, !monthly);
    m_tree->setColumnHidden(7, !monthly);
    loadRecords();
    refreshPayBtn();
    refreshCheckButtons();
}

void AttendanceTab::loadRecords() {
    if (m_employeeId <= 0) {
        m_tree->blockSignals(true);
        m_tree->clear();
        m_tree->blockSignals(false);
        m_mixedModesBanner->setVisible(false);
        onItemSelectionChanged();
        return;
    }
    populateTree(AttendanceRepository::instance().getRecordsForMonth(
        m_employeeId, m_year, m_month));

    // Show mixed-modes banner for monthly employees only
    if (m_employee.isMonthly()) {
        const auto s = AttendanceRepository::instance()
                           .getMonthlySummary(m_employeeId, m_year, m_month);
        m_mixedModesBanner->setVisible(s.hasMixedModes);
    } else {
        m_mixedModesBanner->setVisible(false);
    }
}

void AttendanceTab::refreshPayBtn() {
    if (m_employeeId <= 0) { m_markMonthPaidBtn->setEnabled(false); return; }
    auto s = AttendanceRepository::instance()
                 .getMonthlySummary(m_employeeId, m_year, m_month);
    m_markMonthPaidBtn->setEnabled(s.unpaidAmount > 0);
}

// ── populateTree ──────────────────────────────────────────────────────────
//
// Groups records by date.  For each date:
//   • 1 record  → single top-level item, no expand arrow, looks like the old table row
//   • N records → top-level "day" item showing totals + "(N sessions)" badge,
//                 with N child session items underneath
//
// Paid status logic for multi-session days:
//   all paid   → "Paid"    (green)
//   all unpaid → "Unpaid"  (default)
//   mixed      → "Partial" (amber)

void AttendanceTab::populateTree(const QVector<AttendanceRecord>& records) {
    // Block all signals during rebuild to prevent itemSelectionChanged
    // firing on partially constructed items during clear() and insertions.
    m_tree->blockSignals(true);

    // Preserve expanded state across reloads
    QSet<QString> expandedDates;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (item->isExpanded())
            expandedDates.insert(item->data(0, kRoleId).toString());
    }

    m_tree->clear();

    // Group by date (records are already sorted ASC by date from the repo)
    QMap<QDate, QVector<AttendanceRecord>> byDate;
    for (const auto& r : records)
        byDate[r.date].append(r);

    const bool dark = ThemeHelper::isDark();
    const QColor amberFg = dark ? QColor(255, 204, 128) : QColor(180, 100,  0);
    const QColor amberBg = dark ? QColor( 61,  40,   0) : QColor(255, 248, 220);
    const QColor greenFg = dark ? QColor(102, 217, 140) : QColor( 30, 132,  73);
    const QColor greenBg = dark ? QColor(  0,  40,  20) : QColor(233, 247, 239);

    for (auto it = byDate.begin(); it != byDate.end(); ++it) {
        const QDate date          = it.key();
        const auto& dayRecords    = it.value();
        const int   sessionCount  = dayRecords.size();
        const bool  multiSession  = sessionCount > 1;

        // ── Aggregate day totals ──────────────────────────────────────────
        double totalHours = 0.0, totalWage = 0.0;
        double totalBase = 0.0, totalDed = 0.0;
        int paidCount = 0;
        bool anyOpen  = false;
        for (const auto& r : dayRecords) {
            if (r.isOpen()) { anyOpen = true; continue; }
            totalHours += r.hoursWorked;
            totalBase  += r.baseDailyRate;
            totalDed   += r.dayDeduction;
            totalWage  += r.dailyWage;
            if (r.paid) ++paidCount;
        }
        const int closedCount = sessionCount - (anyOpen ? 1 : 0);

        // ── Day-level paid status ─────────────────────────────────────────
        QString dayStatus;
        QColor  dayFg, dayBg;
        bool    colorDay = false;

        if (anyOpen) {
            dayStatus = tr("Open");
        } else if (paidCount == closedCount && closedCount > 0) {
            dayStatus = tr("Paid");
            dayFg = greenFg; dayBg = greenBg; colorDay = true;
        } else if (paidCount == 0) {
            dayStatus = tr("Unpaid");
        } else {
            dayStatus = tr("Partial");
            dayFg = amberFg; dayBg = amberBg; colorDay = true;
        }

        // ── Build date label ──────────────────────────────────────────────
        QString dateLabel = date.toString("yyyy-MM-dd");
        if (multiSession)
            dateLabel += QString("  (%1 %2)").arg(sessionCount).arg(tr("sessions"));

        // ── Top-level day item ────────────────────────────────────────────
        auto* dayItem = new QTreeWidgetItem(m_tree);
        dayItem->setData(0, kRoleId,        date.toString(Qt::ISODate));
        dayItem->setData(0, kRoleIsSession, false);
        dayItem->setText(0, dateLabel);
        dayItem->setTextAlignment(0, Qt::AlignVCenter | Qt::AlignLeft);

        // Helper: build the Late/Early display string for a single record
        const bool isMonthly = m_employee.isMonthly();
        auto lateEarlyText = [&](const AttendanceRecord& r) -> QString {
            if (!isMonthly || r.isOpen()) return QString();
            QStringList parts;
            if (r.lateMinutes  > 0) parts << tr("+%1L").arg(r.lateMinutes);
            if (r.earlyMinutes > 0) parts << tr("-%1E").arg(r.earlyMinutes);
            return parts.isEmpty() ? QString() : parts.join(" ");
        };

        // Helper: format a monetary value or "--" depending on visibility/open state
        auto fmtWage = [&](double val) -> QString {
            return wagesVisible() ? CurrencyManager::format(val) : QStringLiteral("--");
        };

        if (multiSession) {
            // Show aggregated totals on the day row
            dayItem->setText(1, "");   // no single check-in for multi
            dayItem->setText(2, "");
            dayItem->setText(3, anyOpen ? "--" : QString::number(totalHours, 'f', 2));
            // Monthly: show base / deduction / net columns
            if (isMonthly) {
                dayItem->setText(4, anyOpen ? "--" : fmtWage(totalBase));
                dayItem->setText(5, anyOpen ? "--" : fmtWage(totalDed));
                dayItem->setText(6, anyOpen ? "--" : fmtWage(totalWage));
            } else {
                dayItem->setText(6, anyOpen ? "--" : fmtWage(totalWage));
            }
            dayItem->setText(7, "");   // Late/Early — shown per session
        } else {
            // Single session — show times directly on day row
            const auto& r = dayRecords.first();
            dayItem->setText(1, r.checkIn.toString("hh:mm AP"));
            if (r.isOpen()) {
                dayItem->setText(2, "--");
                dayItem->setText(3, "--");
                dayItem->setText(4, "--");
                dayItem->setText(5, "--");
                dayItem->setText(6, "--");
                dayItem->setText(7, "");
            } else {
                dayItem->setText(2, r.checkOut.toString("hh:mm AP"));
                dayItem->setText(3, QString::number(r.hoursWorked, 'f', 2));
                if (isMonthly) {
                    dayItem->setText(4, fmtWage(r.baseDailyRate));
                    dayItem->setText(5, fmtWage(r.dayDeduction));
                    dayItem->setText(6, fmtWage(r.dailyWage));
                } else {
                    dayItem->setText(6, fmtWage(r.dailyWage));
                }
                dayItem->setText(7, lateEarlyText(r));
            }
            // Store record id on single-session day item too for convenience
            dayItem->setData(0, kRoleId,        r.id);
            dayItem->setData(0, kRoleIsSession, true);
        }

        for (int col = 0; col < 9; ++col)
            dayItem->setTextAlignment(col, Qt::AlignCenter | Qt::AlignVCenter);
        dayItem->setTextAlignment(0, Qt::AlignVCenter | Qt::AlignLeft);

        dayItem->setText(8, dayStatus);
        if (colorDay) {
            for (int col = 0; col < 9; ++col) {
                dayItem->setForeground(col, dayFg);
                dayItem->setBackground(col, dayBg);
            }
        }

        // ── Child session items (only for multi-session days) ─────────────
        if (multiSession) {
            for (int s = 0; s < dayRecords.size(); ++s) {
                const auto& r    = dayRecords[s];
                auto* sessItem   = new QTreeWidgetItem(dayItem);
                sessItem->setData(0, kRoleId,        r.id);
                sessItem->setData(0, kRoleIsSession, true);

                sessItem->setText(0, QString("  #%1").arg(s + 1));
                sessItem->setTextAlignment(0, Qt::AlignVCenter | Qt::AlignLeft);

                sessItem->setText(1, r.checkIn.toString("hh:mm AP"));
                sessItem->setTextAlignment(1, Qt::AlignCenter);

                if (r.isOpen()) {
                    sessItem->setText(2, "--");
                    sessItem->setText(3, "--");
                    sessItem->setText(4, "--");
                    sessItem->setText(5, "--");
                    sessItem->setText(6, "--");
                    sessItem->setText(7, "");
                    sessItem->setText(8, tr("Open"));
                } else {
                    sessItem->setText(2, r.checkOut.toString("hh:mm AP"));
                    sessItem->setTextAlignment(2, Qt::AlignCenter);
                    sessItem->setText(3, QString::number(r.hoursWorked, 'f', 2));
                    sessItem->setTextAlignment(3, Qt::AlignCenter);
                    if (isMonthly) {
                        sessItem->setText(4, fmtWage(r.baseDailyRate));
                        sessItem->setTextAlignment(4, Qt::AlignCenter);
                        sessItem->setText(5, fmtWage(r.dayDeduction));
                        sessItem->setTextAlignment(5, Qt::AlignCenter);
                        sessItem->setText(6, fmtWage(r.dailyWage));
                        sessItem->setTextAlignment(6, Qt::AlignCenter);
                    } else {
                        sessItem->setText(6, fmtWage(r.dailyWage));
                        sessItem->setTextAlignment(6, Qt::AlignCenter);
                    }
                    sessItem->setText(7, lateEarlyText(r));
                    sessItem->setTextAlignment(7, Qt::AlignCenter);

                    const QString sessStatus = r.paid ? tr("Paid") : tr("Unpaid");
                    sessItem->setText(8, sessStatus);
                    sessItem->setTextAlignment(8, Qt::AlignCenter);
                    if (r.paid) {
                        sessItem->setForeground(8, greenFg);
                        sessItem->setBackground(8, greenBg);
                    }
                }
            }

            // Restore expanded state
            dayItem->setExpanded(expandedDates.contains(date.toString(Qt::ISODate)));
        }
    }

    // Unblock signals and manually sync button state
    m_tree->blockSignals(false);
    onItemSelectionChanged();
}

// ── Selection helpers ─────────────────────────────────────────────────────

void AttendanceTab::onItemSelectionChanged() {
    // Guard: buttons may not be constructed yet during early signal emissions
    if (!m_editBtn || !m_deleteBtn || !m_markPaidBtn) return;

    const auto ids    = selectedRecordIds();
    const bool any    = !ids.isEmpty();
    const bool single = ids.size() == 1;

    m_editBtn->setEnabled(single);
    m_deleteBtn->setEnabled(any);
    m_markPaidBtn->setEnabled(any);
}

QVector<int> AttendanceTab::selectedRecordIds() const {
    QVector<int> ids;
    for (auto* item : m_tree->selectedItems()) {
        if (item->data(0, kRoleIsSession).toBool()) {
            const int id = item->data(0, kRoleId).toInt();
            if (id > 0 && !ids.contains(id))
                ids.append(id);
        } else {
            // Day item selected — collect all child session ids
            for (int i = 0; i < item->childCount(); ++i) {
                const int id = item->child(i)->data(0, kRoleId).toInt();
                if (id > 0 && !ids.contains(id))
                    ids.append(id);
            }
        }
    }
    return ids;
}

int AttendanceTab::currentRecordId() const {
    const auto sel = m_tree->selectedItems();
    if (sel.isEmpty()) return -1;
    auto* item = sel.first();
    // Only return a record id if a single session leaf is selected
    if (item->data(0, kRoleIsSession).toBool())
        return item->data(0, kRoleId).toInt();
    return -1;
}

QVector<int> AttendanceTab::dayRecordIds(QTreeWidgetItem* dayItem) const {
    QVector<int> ids;
    if (!dayItem) return ids;
    if (dayItem->data(0, kRoleIsSession).toBool()) {
        // Single-session day item
        ids.append(dayItem->data(0, kRoleId).toInt());
    } else {
        for (int i = 0; i < dayItem->childCount(); ++i)
            ids.append(dayItem->child(i)->data(0, kRoleId).toInt());
    }
    return ids;
}

// ── Context menu ──────────────────────────────────────────────────────────

void AttendanceTab::onContextMenu(const QPoint& pos) {
    auto ids = selectedRecordIds();
    if (ids.isEmpty()) return;

    const auto allRecords = AttendanceRepository::instance()
                                .getRecordsForMonth(m_employeeId, m_year, m_month);

    int paidCount = 0, unpaidCount = 0;
    for (const auto& r : allRecords) {
        if (ids.contains(r.id))
            r.paid ? ++paidCount : ++unpaidCount;
    }

    QMenu menu(this);
    auto* markPaidAction   = menu.addAction(tr("Mark as Paid"));
    auto* markUnpaidAction = menu.addAction(tr("Mark as Unpaid"));
    markPaidAction->setEnabled(unpaidCount > 0);
    markUnpaidAction->setEnabled(paidCount > 0);

    connect(markPaidAction, &QAction::triggered, this, [this, ids]() {
        if (!guardMarkPaid()) return;
        AttendanceRepository::instance().markPaidMultiple(ids);
        loadRecords();
        refreshPayBtn();
        emit attendanceChanged();
    });
    connect(markUnpaidAction, &QAction::triggered, this, [this, ids]() {
        if (!guardAdmin(LockPolicy::Feature::MarkUnpaid)) return;
        AttendanceRepository::instance().markUnpaidMultiple(ids);
        loadRecords();
        refreshPayBtn();
        emit attendanceChanged();
    });

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ── Month changed ─────────────────────────────────────────────────────────

void AttendanceTab::onMonthChanged() {
    m_month = m_monthCombo->currentData().toInt();
    m_year  = m_yearCombo->currentData().toInt();
    loadRecords();
    refreshPayBtn();
    emit monthChanged(m_year, m_month);
}

// ── CRUD ──────────────────────────────────────────────────────────────────

void AttendanceTab::onAddRecord() {
    if (!guardAdmin(LockPolicy::Feature::AddAttendance)) return;
    if (m_employeeId <= 0) return;
    AttendanceDialog dlg(m_employee, this);
    if (dlg.exec() == QDialog::Accepted) {
        AttendanceRecord rec = dlg.record();
        if (AttendanceRepository::instance().addRecord(rec)) {
            loadRecords();
            refreshPayBtn();
            refreshCheckButtons();
            emit attendanceChanged();
        } else {
            QMessageBox::warning(this, tr("Cannot Add Record"),
                AttendanceRepository::instance().lastError());
        }
    }
}

void AttendanceTab::onEditRecord() {
    if (!guardAdmin(LockPolicy::Feature::EditAttendance)) return;
    const int id = currentRecordId();
    if (id <= 0) return;

    const auto records = AttendanceRepository::instance()
                             .getRecordsForMonth(m_employeeId, m_year, m_month);
    for (const auto& r : records) {
        if (r.id == id) {
            AttendanceDialog dlg(r, m_employee, this);
            if (dlg.exec() == QDialog::Accepted) {
                AttendanceRecord updated = dlg.record();
                if (AttendanceRepository::instance().updateRecord(updated)) {
                    loadRecords();
                    refreshPayBtn();
                    refreshCheckButtons();
                    emit attendanceChanged();
                } else {
                    QMessageBox::critical(this, tr("Error"),
                        tr("Failed to update record:\n%1")
                            .arg(AttendanceRepository::instance().lastError()));
                }
            }
            return;
        }
    }
}

void AttendanceTab::onDeleteRecord() {
    if (!guardAdmin(LockPolicy::Feature::DeleteAttendance)) return;
    const auto ids = selectedRecordIds();
    if (ids.isEmpty()) return;
    const auto reply = QMessageBox::question(this, tr("Confirm Delete"),
        tr("Delete %n selected records?", nullptr, ids.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        int failedCount = 0;
        for (int id : ids) {
            if (!AttendanceRepository::instance().deleteRecord(id))
                ++failedCount;
        }
        loadRecords();
        refreshPayBtn();
        refreshCheckButtons();
        emit attendanceChanged();
        if (failedCount > 0)
            QMessageBox::warning(this, tr("Delete Incomplete"),
                tr("%n record(s) could not be deleted and were skipped.", nullptr, failedCount));
    }
}

void AttendanceTab::onMarkPaid() {
    if (!guardMarkPaid()) return;
    const auto ids = selectedRecordIds();
    if (ids.isEmpty()) return;
    AttendanceRepository::instance().markPaidMultiple(ids);
    loadRecords();
    refreshPayBtn();
    emit attendanceChanged();
}
