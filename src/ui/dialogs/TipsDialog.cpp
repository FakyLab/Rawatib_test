#include "ui/dialogs/TipsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QRandomGenerator>
#include <QFont>
#include <QPalette>
#include <QCoreApplication>
#include <QSizePolicy>
#include <QGuiApplication>
#include <QSettings>

// ── Tip data ───────────────────────────────────────────────────────────────
//
// IMPORTANT: Do NOT use raw UTF-8 byte escapes (\xNN) in these strings.
// QT_TRANSLATE_NOOP registers the exact string as a lookup key. If you use
// byte escapes, the key won't match the .ts file and Qt silently falls back
// to English — even on Arabic/Korean/Japanese builds.
// Use the actual Unicode characters directly (→ not \xe2\x86\x92, etc.)

struct Tip {
    const char* icon;
    const char* title;
    const char* body;
};

static const Tip s_tips[] = {

    // ── Security ──────────────────────────────────────────────────────────

    {
        "🔑",
        QT_TRANSLATE_NOOP("TipsDialog", "Generate your recovery file now"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "If you ever forget your admin password, the recovery file is your only way back in. "
            "Go to Settings → Admin Password → Generate Recovery File and store it on a USB drive "
            "or secure cloud — not on the same machine as the database.")
    },
    {
        "🔒",
        QT_TRANSLATE_NOOP("TipsDialog", "Lock policy is your first line of defense"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Advanced → Kiosk & Lock Policy lets you decide exactly which actions require admin unlock. "
            "Lock editing and deleting attendance so staff can check in freely but can't alter records. "
            "Your data stays clean even on a shared machine.")
    },
    {
        "👁",
        QT_TRANSLATE_NOOP("TipsDialog", "Hide wages from staff at the screen"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Enable \"Employee wages & salary figures\" in Lock Policy → Visibility "
            "and wages disappear from the employee list, attendance tree, and salary summary when the admin is locked. "
            "Employees with a PIN can still view their own wages privately using \"Show My Wages\".")
    },
    {
        "📋",
        QT_TRANSLATE_NOOP("TipsDialog", "Every sensitive action is recorded"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Advanced → Audit Log keeps a tamper-evident record of every add, edit, delete, "
            "payment, import, backup, and security change — with timestamps. "
            "Use \"Verify Integrity\" to confirm no entries have been silently modified or deleted.")
    },
    {
        "🛡",
        QT_TRANSLATE_NOOP("TipsDialog", "Too many wrong passwords? The app locks itself"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "After 5 failed password attempts the app enforces a lockout that grows with each retry — "
            "30 seconds, then 2 minutes, up to 2 hours. "
            "This protects your data even if someone sits at the machine while you're away.")
    },

    // ── Export & Import ───────────────────────────────────────────────────

    {
        "📤",
        QT_TRANSLATE_NOOP("TipsDialog", "CSV is the format that comes back in"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Rawatib can import attendance from CSV files — not XLSX. "
            "When exporting for the purpose of re-importing, always choose CSV. "
            "XLSX is the beautiful report you print or share; CSV is the data you move.")
    },
    {
        "🔄",
        QT_TRANSLATE_NOOP("TipsDialog", "Export → Reset → Import: your last resort"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Forgot your password <b>and</b> lost your recovery file? "
            "Export all employee CSVs first via File → Export while you still have database access. "
            "Then run the app with "
            "<code style='background:#1e1e1e;color:#d4d4d4;padding:1px 5px;"
            "border-radius:3px;font-family:monospace;'>--reset-all</code> "
            "to wipe and restart, then import your CSVs back. Your attendance history survives.")
    },
    {
        "🔑",
        QT_TRANSLATE_NOOP("TipsDialog", "Lost your password? The recovery file gets you back in"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "If you forgot your admin password but still have the recovery file, "
            "run the app from the command line with "
            "<code style='background:#1e1e1e;color:#d4d4d4;padding:1px 5px;"
            "border-radius:3px;font-family:monospace;'>--bypass-key --recovery-file path/to/file.rwtrec</code> "
            "to regain access instantly. Your data stays completely intact — no rebuild needed.")
    },
    {
        "📥",
        QT_TRANSLATE_NOOP("TipsDialog", "Import is smarter than you think"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "When you import a CSV, Rawatib checks every record against the existing database — "
            "detecting overlaps, wage mismatches, and unknown employees. "
            "You review and resolve each conflict before a single record is written.")
    },
    {
        "💾",
        QT_TRANSLATE_NOOP("TipsDialog", "Wages missing from your CSV? Enter them on import"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "If you exported with \"Hide Wages\" active, the CSV won't contain wage data. "
            "When importing that file into a fresh database, the import preview lets you "
            "enter each employee's hourly wage manually — and daily wages recalculate automatically.")
    },
    {
        "📊",
        QT_TRANSLATE_NOOP("TipsDialog", "The XLSX export is a live spreadsheet"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Open the exported XLSX file and change the hourly wage cell — "
            "every daily wage in the sheet recalculates instantly via formulas. "
            "It's a full working payroll report, not just a static table.")
    },

    // ── Payroll & Calculations ────────────────────────────────────────────

    {
        "🧮",
        QT_TRANSLATE_NOOP("TipsDialog", "How daily wages are calculated"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Rawatib calculates each session: check-out time minus check-in time gives hours worked, "
            "multiplied by the employee's hourly wage to get the daily wage. "
            "Edit the hourly wage in the employee profile and re-import to recalculate historical records.")
    },
    {
        "➕",
        QT_TRANSLATE_NOOP("TipsDialog", "Add deductions and bonuses to net pay"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Advanced → Payroll Rules lets you define recurring deductions (social insurance, tax) "
            "and additions (bonuses, allowances) — either as fixed amounts or a percentage of gross salary. "
            "The Salary Summary tab shows the full breakdown and calculates net pay automatically.")
    },
    {
        "💳",
        QT_TRANSLATE_NOOP("TipsDialog", "Pay an entire month in one click"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "The \"Pay Entire Month\" button in the Attendance tab marks all unpaid records "
            "for the selected employee and month as paid at once — no need to go record by record. "
            "A payment slip is printed automatically if you confirm.")
    },
    {
        "📅",
        QT_TRANSLATE_NOOP("TipsDialog", "Multiple check-ins in one day? No problem"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Rawatib supports multiple sessions per day — morning and afternoon shifts, overtime, "
            "or any split-day arrangement. Each session is tracked separately and the daily total "
            "is summed automatically in the attendance tree.")
    },

    // ── Backup & Data Safety ──────────────────────────────────────────────

    {
        "🗄",
        QT_TRANSLATE_NOOP("TipsDialog", "Auto-backup runs silently in the background"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Rawatib automatically backs up your database on each launch if enough time has passed. "
            "Find all auto-backups in File → Open Auto-Backup Folder. "
            "Keep a few recent backups on a separate drive just in case.")
    },
    {
        "⚠️",
        QT_TRANSLATE_NOOP("TipsDialog", "Always backup before restoring"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Restoring a database replaces everything currently in it. "
            "Use File → Backup Database first to save your current state — "
            "then if the restore doesn't go as expected, you can get back to where you were.")
    },

    // ── Kiosk & Employee PIN ──────────────────────────────────────────────

    {
        "🖥",
        QT_TRANSLATE_NOOP("TipsDialog", "Turn Rawatib into a self-service kiosk"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Lock \"Check-in\" and \"Check-out\" in Lock Policy, enable the kiosk PIN feature, "
            "and set a personal PIN for each employee. "
            "Now staff can clock in and out themselves using their PIN — no admin involvement needed.")
    },
    {
        "🔢",
        QT_TRANSLATE_NOOP("TipsDialog", "Employee PINs are set per person"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Each employee can have their own 6–12 digit PIN, set from Edit Employee. "
            "PINs are hashed and stored securely — even you can't read them back. "
            "Employees use their PIN for kiosk check-in, self-pay marking, or viewing their own wages.")
    },

    // ── Monthly Salary ────────────────────────────────────────────────────

    {
        "📆",
        QT_TRANSLATE_NOOP("TipsDialog", "Monthly salary employees work differently"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Set an employee's pay type to Monthly Salary in Edit Employee "
            "and Rawatib switches to fixed-salary mode for that person. "
            "Instead of hours × rate, it tracks presence against expected working days "
            "and deducts proportionally for absences and late arrivals.")
    },
    {
        "⏰",
        QT_TRANSLATE_NOOP("TipsDialog", "Late arrivals are deducted automatically"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "For monthly employees, set the expected check-in and check-out times in Edit Employee. "
            "Rawatib calculates how many minutes late or early each day and deducts the exact "
            "proportional amount from that day's wage — visible in the Late/Early column.")
    },
    {
        "🗓",
        QT_TRANSLATE_NOOP("TipsDialog", "Public holidays won't count as absences"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Add public holidays and approved leave in Advanced → Day Exceptions. "
            "Any date you mark there is excluded from the absent-day count for all employees — "
            "or just one specific person if you select them. No hardcoded calendars, works for any country.")
    },

    // ── Interface & Workflow ──────────────────────────────────────────────

    {
        "🌐",
        QT_TRANSLATE_NOOP("TipsDialog", "Full Arabic interface available"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Switch to Arabic from View → Language → Arabic. "
            "The entire interface — menus, dialogs, reports, and exports — "
            "switches to Arabic with right-to-left layout instantly.")
    },
    {
        "💱",
        QT_TRANSLATE_NOOP("TipsDialog", "Set your local currency once"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Go to View → Currency and set your currency symbol. "
            "It appears everywhere — employee list, salary reports, exports, and printed slips. "
            "You only need to set it once.")
    },
    {
        "⏱",
        QT_TRANSLATE_NOOP("TipsDialog", "Auto-lock keeps the admin session safe"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Settings → Auto-lock Timeout automatically re-locks the admin session after a period of inactivity. "
            "Set a short timeout on shared machines so the admin doesn't accidentally leave the session open.")
    },
    {
        "🖨",
        QT_TRANSLATE_NOOP("TipsDialog", "Print reports directly from Salary Summary"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "The Salary Summary tab has a Print button that generates a formatted monthly report "
            "sized for A4 or receipt paper automatically based on your printer. "
            "It includes attendance records, totals, and net pay if payroll rules are enabled.")
    },
    {
        "🔍",
        QT_TRANSLATE_NOOP("TipsDialog", "Search employees instantly"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "The search box above the employee list filters by name or phone number as you type. "
            "On a busy machine with dozens of employees, it's the fastest way to jump to the right person.")
    },
    {
        "✏️",
        QT_TRANSLATE_NOOP("TipsDialog", "Attendance records are always editable"),
        QT_TRANSLATE_NOOP("TipsDialog",
            "Made a mistake on a check-in or check-out time? Select the record and click Edit Record. "
            "Rawatib recalculates hours and daily wage automatically when you save. "
            "Editing is admin-locked by default to protect data integrity.")
    },
};

static constexpr int TIP_COUNT = static_cast<int>(sizeof(s_tips) / sizeof(s_tips[0]));


static const char* kEverOpened = "tips/everOpened";
static const char* kSeenEver   = "tips/seenEver";  // comma-separated indices

// ── Dialog ─────────────────────────────────────────────────────────────────

TipsDialog::TipsDialog(QWidget* parent)
    : QDialog(parent)
    , m_tipCount(TIP_COUNT)
{
    setWindowTitle(tr("Discover Rawatib"));
    setupUi();

    QSettings s;

    // Load which tips have been seen across all previous sessions
    const QString seenStr = s.value(kSeenEver, QString()).toString();
    if (!seenStr.isEmpty()) {
        for (const QString& part : seenStr.split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const int idx = part.toInt(&ok);
            if (ok && idx >= 0 && idx < m_tipCount)
                m_seenEver.append(idx);
        }
    }

    const bool everOpened = s.value(kEverOpened, false).toBool();
    if (!everOpened) {
        s.setValue(kEverOpened, true);
        m_currentIndex = 0;
    } else {
        m_currentIndex = pickNext();
    }

    // Mark current tip as seen and persist
    if (!m_seenEver.contains(m_currentIndex))
        m_seenEver.append(m_currentIndex);
    saveSeen();

    displayTip(m_currentIndex);
}

void TipsDialog::setupUi()
{
    setFixedWidth(440);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ── Card area ──────────────────────────────────────────────────────────
    auto* card = new QWidget(this);
    card->setObjectName("tipCard");
    card->setStyleSheet(
        "QWidget#tipCard { background: palette(base); border-bottom: 1px solid palette(mid); }");
    card->setFixedHeight(CARD_H);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(10);
    cardLayout->setContentsMargins(28, 24, 28, 24);

    m_iconLabel = new QLabel(card);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(38);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setFixedHeight(54);
    cardLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(card);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    cardLayout->addWidget(m_titleLabel);

    cardLayout->addSpacing(4);

    m_bodyLabel = new QLabel(card);
    m_bodyLabel->setTextFormat(Qt::RichText);
    m_bodyLabel->setAlignment(Qt::AlignJustify | Qt::AlignTop);
    m_bodyLabel->setWordWrap(true);
    m_bodyLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_bodyLabel->setStyleSheet("color: palette(shadow); line-height: 1.4;");
    m_bodyLabel->setLayoutDirection(QGuiApplication::layoutDirection());
    cardLayout->addWidget(m_bodyLabel);

    cardLayout->addStretch();
    mainLayout->addWidget(card);

    // ── Dot indicators ─────────────────────────────────────────────────────
    m_dotsWidget = new QWidget(this);
    m_dotsWidget->setStyleSheet("background: palette(window);");
    auto* dotsLayout = new QHBoxLayout(m_dotsWidget);
    dotsLayout->setContentsMargins(0, 8, 0, 4);
    dotsLayout->setSpacing(5);
    dotsLayout->addStretch();

    for (int i = 0; i < m_tipCount; ++i) {
        auto* dot = new QLabel(m_dotsWidget);
        dot->setFixedSize(7, 7);
        m_dots.append(dot);
        dotsLayout->addWidget(dot);
    }
    dotsLayout->addStretch();
    mainLayout->addWidget(m_dotsWidget);

    // ── Bottom bar ─────────────────────────────────────────────────────────
    auto* bottomBar    = new QWidget(this);
    bottomBar->setStyleSheet("background: palette(window);");
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(16, 6, 16, 10);
    bottomLayout->setSpacing(8);

    bottomLayout->addStretch();

    m_counterLabel = new QLabel(bottomBar);
    m_counterLabel->setStyleSheet("color: palette(mid); font-size: 9pt;");
    bottomLayout->addWidget(m_counterLabel);

    m_anotherBtn = new QPushButton(tr("Show Another"), bottomBar);
    m_anotherBtn->setDefault(false);
    m_anotherBtn->setAutoDefault(false);
    connect(m_anotherBtn, &QPushButton::clicked, this, &TipsDialog::showAnother);
    bottomLayout->addWidget(m_anotherBtn);

    auto* closeBtn = new QPushButton(tr("Close"), bottomBar);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(closeBtn);

    mainLayout->addWidget(bottomBar);
}

// ── Tip display ────────────────────────────────────────────────────────────

void TipsDialog::displayTip(int index)
{
    const Tip& tip = s_tips[index];
    m_iconLabel->setText(QString::fromUtf8(tip.icon));
    m_titleLabel->setText(QCoreApplication::translate("TipsDialog", tip.title));
    m_bodyLabel->setText(QCoreApplication::translate("TipsDialog", tip.body));
    m_counterLabel->setText(tr("Tip %1 of %2").arg(index + 1).arg(m_tipCount));
    updateDots();
}

void TipsDialog::updateDots()
{
    for (int i = 0; i < m_dots.size(); ++i) {
        const bool active = (i == m_currentIndex);
        const bool seen   = m_seenEver.contains(i);
        QString style;
        if (active)
            style = "background: palette(highlight); border-radius: 3px;";
        else if (seen)
            style = "background: palette(mid); border-radius: 3px;";
        else
            style = "background: palette(midlight); border-radius: 3px;";
        m_dots[i]->setStyleSheet(style);
    }
}

void TipsDialog::saveSeen()
{
    QStringList parts;
    for (int idx : m_seenEver)
        parts.append(QString::number(idx));
    QSettings().setValue(kSeenEver, parts.join(','));
}

// ── Navigation ─────────────────────────────────────────────────────────────

int TipsDialog::pickNext() const
{
    if (m_tipCount <= 1) return 0;

    // Prefer tips never seen before across all sessions
    QVector<int> unseen;
    for (int i = 0; i < m_tipCount; ++i)
        if (!m_seenEver.contains(i))
            unseen.append(i);

    // If everything has been seen, pick any tip except the current one
    if (unseen.isEmpty()) {
        for (int i = 0; i < m_tipCount; ++i)
            if (i != m_currentIndex) unseen.append(i);
    }

    return unseen[static_cast<int>(
        QRandomGenerator::global()->bounded(static_cast<quint32>(unseen.size())))];
}

void TipsDialog::showAnother()
{
    if (m_tipCount <= 1) return;

    m_currentIndex = pickNext();

    if (!m_seenEver.contains(m_currentIndex))
        m_seenEver.append(m_currentIndex);
    saveSeen();

    displayTip(m_currentIndex);
}
