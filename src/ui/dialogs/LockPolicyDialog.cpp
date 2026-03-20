#include "ui/dialogs/LockPolicyDialog.h"
#include "utils/LockPolicy.h"
#include "utils/EmployeePinManager.h"
#include "utils/AuditLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>

// ── Constructor ───────────────────────────────────────────────────────────

LockPolicyDialog::LockPolicyDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Kiosk & Lock Policy"));
    setMinimumWidth(460);
    setMaximumWidth(700);
    setMinimumHeight(500);
    resize(520, 600);
    setSizeGripEnabled(true);
    setupUi();
}

// ── UI Setup ──────────────────────────────────────────────────────────────

void LockPolicyDialog::setupUi() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(12);
    outerLayout->setContentsMargins(16, 16, 16, 12);

    // ── Intro label ───────────────────────────────────────────────────────
    auto* intro = new QLabel(
        tr("Control which actions require the admin lock to be open. "
           "Changes take effect immediately. Defaults reflect the standard "
           "secure configuration."), this);
    intro->setWordWrap(true);
    outerLayout->addWidget(intro);

    // ── Scroll area for the three groups ─────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* scrollContents = new QWidget(scroll);
    auto* scrollLayout   = new QVBoxLayout(scrollContents);
    scrollLayout->setSpacing(12);
    scrollLayout->setContentsMargins(0, 0, 0, 0);

    // ── Helper: build one group ───────────────────────────────────────────
    // groupIndex: 0=Employee, 1=Attendance, 2=Data&Settings
    auto buildGroup = [&](int groupIndex,
                          const QString& title,
                          const QString& subtitle,
                          const QVector<QPair<LockPolicy::Feature, QString>>& items)
    {
        Group g;

        auto* box    = new QGroupBox(title, scrollContents);
        auto* layout = new QVBoxLayout(box);
        layout->setSpacing(4);
        layout->setContentsMargins(12, 8, 12, 10);

        // Subtitle + master toggle on the same row
        auto* topRow    = new QHBoxLayout();
        auto* subLabel  = new QLabel(subtitle, box);
        subLabel->setStyleSheet("color: palette(mid);");

        g.masterCheck = new QCheckBox(box);
        g.masterCheck->setTristate(true);
        g.masterCheck->setToolTip(tr("Toggle all"));

        topRow->addWidget(subLabel, 1);
        topRow->addWidget(g.masterCheck);
        layout->addLayout(topRow);

        // Separator line
        auto* line = new QFrame(box);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        layout->addWidget(line);

        // Feature rows
        for (const auto& [feature, label] : items) {
            FeatureRow row;
            row.feature = feature;
            row.check   = new QCheckBox(label, box);
            row.check->setChecked(LockPolicy::isLocked(feature));

            const int fi = static_cast<int>(feature); // capture by value
            connect(row.check, &QCheckBox::toggled, this, [this, feature](bool checked) {
                onFeatureToggled(feature, checked);
            });

            layout->addWidget(row.check);
            g.rows.append(row);
        }

        // Wire master toggle
        const int gi = groupIndex;
        connect(g.masterCheck, &QCheckBox::checkStateChanged, this, [this, gi](Qt::CheckState state) {
            onGroupToggled(gi, state);
        });

        updateGroupMaster(g);
        m_groups.append(g);
        scrollLayout->addWidget(box);
    };

    // ── Group 0: Employee Management ─────────────────────────────────────
    buildGroup(0,
        tr("Employee Management"),
        tr("Require admin unlock for:"),
        {
            { LockPolicy::Feature::AddEmployee,    tr("Adding employees")    },
            { LockPolicy::Feature::EditEmployee,   tr("Editing employees")   },
            { LockPolicy::Feature::DeleteEmployee, tr("Deleting employees")  },
        }
    );

    // ── Group 1: Attendance ───────────────────────────────────────────────
    buildGroup(1,
        tr("Attendance"),
        tr("Require admin unlock for:"),
        {
            { LockPolicy::Feature::AddAttendance,    tr("Adding attendance records")    },
            { LockPolicy::Feature::EditAttendance,   tr("Editing attendance records")   },
            { LockPolicy::Feature::DeleteAttendance, tr("Deleting attendance records")  },
            { LockPolicy::Feature::MarkPaid,         tr("Marking records as paid")      },
            { LockPolicy::Feature::MarkUnpaid,       tr("Marking records as unpaid")    },
            { LockPolicy::Feature::CheckIn,          tr("Check-in")                     },
            { LockPolicy::Feature::CheckOut,         tr("Check-out")                    },
        }
    );

    // ── Group 2: Data & Settings ──────────────────────────────────────────
    buildGroup(2,
        tr("Data & Settings"),
        tr("Require admin unlock for:"),
        {
            { LockPolicy::Feature::ImportAttendance, tr("Importing attendance")     },
            { LockPolicy::Feature::BackupDatabase,   tr("Database backup")          },
            { LockPolicy::Feature::RestoreDatabase,  tr("Database restore")         },
            { LockPolicy::Feature::PayrollRules,     tr("Payroll rules")            },
        }
    );

    // ── Group 3: Visibility ───────────────────────────────────────────────
    buildGroup(3,
        tr("Visibility"),
        tr("Require admin unlock to view:"),
        {
            { LockPolicy::Feature::HideWages, tr("Employee wages, salary figures, and phone numbers") },
        }
    );

    scrollLayout->addStretch();
    scroll->setWidget(scrollContents);
    outerLayout->addWidget(scroll, 1);

    // ── Employee PIN feature toggle ───────────────────────────────────────
    auto* pinSep = new QFrame(this);
    pinSep->setFrameShape(QFrame::HLine);
    pinSep->setFrameShadow(QFrame::Sunken);
    outerLayout->addWidget(pinSep);

    m_employeePinCheck = new QCheckBox(
        tr("Allow employees to mark their own records as paid using their PIN"), this);
    m_employeePinCheck->setChecked(EmployeePinManager::isFeatureEnabled());
    m_employeePinCheck->setToolTip(
        tr("When enabled and Mark Paid is locked, employees can mark only their own\n"
           "records as paid by entering their personal PIN.\n"
           "Set employee PINs via Edit Employee. Has no effect when admin is unlocked."));

    m_kioskPinCheck = new QCheckBox(
        tr("Allow employees to check in and out using their PIN"), this);
    m_kioskPinCheck->setChecked(EmployeePinManager::isKioskPinEnabled());
    m_kioskPinCheck->setToolTip(
        tr("When enabled and Check-in/out is locked, employees can check in and out\n"
           "by entering their personal PIN.\n"
           "Set employee PINs via Edit Employee. Has no effect when admin is unlocked."));

    auto* pinHint = new QLabel(
        tr("Employee PINs are set per-employee in their profile. "
           "Has no effect when the admin session is unlocked."), this);
    pinHint->setWordWrap(true);
    pinHint->setStyleSheet("color: palette(mid); font-size: 9pt;");

    outerLayout->addWidget(m_employeePinCheck);
    outerLayout->addWidget(m_kioskPinCheck);
    outerLayout->addWidget(pinHint);

    connect(m_employeePinCheck, &QCheckBox::toggled, this, [](bool checked) {
        EmployeePinManager::setFeatureEnabled(checked);
    });
    connect(m_kioskPinCheck, &QCheckBox::toggled, this, [](bool checked) {
        EmployeePinManager::setKioskPinEnabled(checked);
    });

    // ── Bottom button row ─────────────────────────────────────────────────
    auto* bottomRow = new QHBoxLayout();

    auto* restoreBtn = new QPushButton(tr("Restore Defaults"), this);
    connect(restoreBtn, &QPushButton::clicked, this, &LockPolicyDialog::onRestoreDefaults);

    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    bottomRow->addWidget(restoreBtn);
    bottomRow->addStretch();
    bottomRow->addWidget(closeBtn);
    outerLayout->addLayout(bottomRow);

    // Set initial enabled state of PIN toggles
    updatePinToggles();
}

// ── Helpers ───────────────────────────────────────────────────────────────

static QString keyForDisplay(LockPolicy::Feature f) {
    switch (f) {
        case LockPolicy::Feature::AddEmployee:      return "add_employee";
        case LockPolicy::Feature::EditEmployee:     return "edit_employee";
        case LockPolicy::Feature::DeleteEmployee:   return "delete_employee";
        case LockPolicy::Feature::AddAttendance:    return "add_attendance";
        case LockPolicy::Feature::EditAttendance:   return "edit_attendance";
        case LockPolicy::Feature::DeleteAttendance: return "delete_attendance";
        case LockPolicy::Feature::MarkPaid:         return "mark_paid";
        case LockPolicy::Feature::MarkUnpaid:       return "mark_unpaid";
        case LockPolicy::Feature::CheckIn:          return "check_in";
        case LockPolicy::Feature::CheckOut:         return "check_out";
        case LockPolicy::Feature::ImportAttendance: return "import_attendance";
        case LockPolicy::Feature::BackupDatabase:   return "backup_database";
        case LockPolicy::Feature::RestoreDatabase:  return "restore_database";
        case LockPolicy::Feature::PayrollRules:     return "payroll_rules";
        case LockPolicy::Feature::HideWages:        return "hide_wages";
    }
    return "unknown";
}

// ── Slots ─────────────────────────────────────────────────────────────────

void LockPolicyDialog::onFeatureToggled(LockPolicy::Feature feature, bool locked) {
    LockPolicy::setLocked(feature, locked);

    AuditLog::record(AuditLog::LOCK_POLICY, "policy", 0,
        QString("Feature \"%1\" set to %2")
            .arg(keyForDisplay(feature))
            .arg(locked ? "locked" : "unlocked"),
        locked ? "unlocked" : "locked",
        locked ? "locked"   : "unlocked");

    for (auto& g : m_groups) {
        for (const auto& row : g.rows) {
            if (row.feature == feature) {
                updateGroupMaster(g);
                break;
            }
        }
    }
    updatePinToggles();
}

void LockPolicyDialog::onGroupToggled(int groupIndex, Qt::CheckState state) {
    if (groupIndex < 0 || groupIndex >= m_groups.size()) return;
    Group& g = m_groups[groupIndex];
    if (g.updating) return;

    // Only act on fully checked or fully unchecked — ignore indeterminate
    if (state == Qt::PartiallyChecked) return;

    const bool locked = (state == Qt::Checked);

    g.updating = true;
    for (auto& row : g.rows) {
        row.check->setChecked(locked);
        LockPolicy::setLocked(row.feature, locked);
    }
    g.updating = false;
    updatePinToggles();
}

void LockPolicyDialog::onRestoreDefaults() {
    const auto reply = QMessageBox::question(this,
        tr("Restore Defaults"),
        tr("Reset all lock settings to their defaults?\n\n"
           "This will restore the standard secure configuration."),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    LockPolicy::resetToDefaults();
    EmployeePinManager::setFeatureEnabled(false);
    EmployeePinManager::setKioskPinEnabled(false);

    // Refresh all checkboxes
    for (auto& g : m_groups) {
        g.updating = true;
        for (auto& row : g.rows)
            row.check->setChecked(LockPolicy::isLocked(row.feature));
        g.updating = false;
        updateGroupMaster(g);
    }
    if (m_employeePinCheck)
        m_employeePinCheck->setChecked(false);
    if (m_kioskPinCheck)
        m_kioskPinCheck->setChecked(false);
    updatePinToggles();
}

// ── Helpers ───────────────────────────────────────────────────────────────

void LockPolicyDialog::updatePinToggles() {
    // ── Mark Paid PIN toggle ──────────────────────────────────────────────
    // Only meaningful when MarkPaid is locked.
    if (m_employeePinCheck) {
        const bool markPaidLocked = LockPolicy::isLocked(LockPolicy::Feature::MarkPaid);
        m_employeePinCheck->setEnabled(markPaidLocked);
        if (markPaidLocked) {
            m_employeePinCheck->setToolTip(
                tr("When enabled, employees can mark only their own records as paid\n"
                   "by entering their personal PIN instead of the admin password.\n"
                   "Set employee PINs via Edit Employee."));
        } else {
            // Uncheck and disable — silently persist the disabled state
            m_employeePinCheck->setChecked(false);
            EmployeePinManager::setFeatureEnabled(false);
            m_employeePinCheck->setToolTip(
                tr("Requires \"Marking records as paid\" to be locked above."));
        }
    }

    // ── Kiosk PIN toggle ──────────────────────────────────────────────────
    // Only meaningful when both CheckIn AND CheckOut are locked.
    if (m_kioskPinCheck) {
        const bool checkInLocked  = LockPolicy::isLocked(LockPolicy::Feature::CheckIn);
        const bool checkOutLocked = LockPolicy::isLocked(LockPolicy::Feature::CheckOut);
        const bool bothLocked     = checkInLocked && checkOutLocked;
        m_kioskPinCheck->setEnabled(bothLocked);
        if (bothLocked) {
            m_kioskPinCheck->setToolTip(
                tr("When enabled, employees can check in and out\n"
                   "by entering their personal PIN instead of the admin password.\n"
                   "Set employee PINs via Edit Employee."));
        } else {
            m_kioskPinCheck->setChecked(false);
            EmployeePinManager::setKioskPinEnabled(false);
            const QString missing = !checkInLocked && !checkOutLocked
                ? tr("Requires \"Check-in\" and \"Check-out\" to be locked above.")
                : !checkInLocked
                    ? tr("Requires \"Check-in\" to be locked above.")
                    : tr("Requires \"Check-out\" to be locked above.");
            m_kioskPinCheck->setToolTip(missing);
        }
    }
}

void LockPolicyDialog::updateGroupMaster(Group& g) {
    if (!g.masterCheck) return;

    int checkedCount = 0;
    for (const auto& row : g.rows)
        if (row.check->isChecked()) ++checkedCount;

    g.updating = true;
    if (checkedCount == 0)
        g.masterCheck->setCheckState(Qt::Unchecked);
    else if (checkedCount == g.rows.size())
        g.masterCheck->setCheckState(Qt::Checked);
    else
        g.masterCheck->setCheckState(Qt::PartiallyChecked);
    g.updating = false;
}
