#include "ui/dialogs/AttendanceDialog.h"
#include "utils/CurrencyManager.h"
#include "utils/DeductionPolicy.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDialogButtonBox>

// ── Add mode constructor ──────────────────────────────────────────────────
AttendanceDialog::AttendanceDialog(const Employee& employee, QWidget* parent)
    : QDialog(parent), m_employee(employee), m_isEditMode(false) {
    setWindowTitle(tr("Add Attendance Record"));
    setMinimumWidth(340);
    setMaximumWidth(480);
    setSizeGripEnabled(false);
    setupUi();
    m_dateEdit->setDate(QDate::currentDate());
    // Pre-fill with expected times for monthly employees, defaults for hourly
    if (employee.isMonthly() && employee.expectedCheckin.isValid())
        m_checkInEdit->setTime(employee.expectedCheckin);
    else
        m_checkInEdit->setTime(QTime(8, 0));
    if (employee.isMonthly() && employee.expectedCheckout.isValid())
        m_checkOutEdit->setTime(employee.expectedCheckout);
    else
        m_checkOutEdit->setTime(QTime(17, 0));
    onTimeChanged();
}

// ── Edit mode constructor ─────────────────────────────────────────────────
AttendanceDialog::AttendanceDialog(const AttendanceRecord& record,
                                   const Employee& employee, QWidget* parent)
    : QDialog(parent), m_employee(employee),
      m_recordId(record.id), m_isEditMode(true) {
    setWindowTitle(tr("Edit Attendance Record"));
    setMinimumWidth(340);
    setMaximumWidth(480);
    setSizeGripEnabled(false);
    setupUi();
    populate(record);
}

void AttendanceDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);

    auto* form = new QFormLayout();

    m_dateEdit = new QDateEdit(this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");

    m_checkInEdit = new QTimeEdit(this);
    m_checkInEdit->setDisplayFormat("hh:mm AP");

    m_checkOutEdit = new QTimeEdit(this);
    m_checkOutEdit->setDisplayFormat("hh:mm AP");

    form->addRow(tr("Date *:"),      m_dateEdit);
    form->addRow(tr("Check-In *:"),  m_checkInEdit);
    form->addRow(tr("Check-Out *:"), m_checkOutEdit);

    if (m_isEditMode) {
        m_noCheckOut = new QCheckBox(tr("No check-out yet (open record)"), this);
        form->addRow(QString(), m_noCheckOut);
        connect(m_noCheckOut, &QCheckBox::toggled,
                this, &AttendanceDialog::onNoCheckOutToggled);
    }

    mainLayout->addLayout(form);

    // ── Preview ───────────────────────────────────────────────────────────
    auto* previewGroup = new QGroupBox(tr("Preview"), this);
    auto* previewForm  = new QFormLayout(previewGroup);

    m_hoursLabel = new QLabel("--", previewGroup);
    m_wageLabel  = new QLabel("--", previewGroup);
    previewForm->addRow(tr("Hours Worked:"), m_hoursLabel);
    previewForm->addRow(tr("Daily Wage:"),   m_wageLabel);

    // Monthly-only: show deduction breakdown in preview
    if (m_employee.isMonthly()) {
        m_lateLabel = new QLabel("--", previewGroup);
        previewForm->addRow(tr("Deduction:"), m_lateLabel);
    }

    mainLayout->addWidget(previewGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    connect(m_checkInEdit,  &QTimeEdit::timeChanged, this, &AttendanceDialog::onTimeChanged);
    connect(m_checkOutEdit, &QTimeEdit::timeChanged, this, &AttendanceDialog::onTimeChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &AttendanceDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void AttendanceDialog::populate(const AttendanceRecord& r) {
    m_dateEdit->setDate(r.date);
    m_checkInEdit->setTime(r.checkIn.isValid() ? r.checkIn : QTime(8, 0));
    if (r.isOpen()) {
        m_checkOutEdit->setTime(QTime(17, 0));
        if (m_noCheckOut) {
            m_noCheckOut->setChecked(true);
            m_checkOutEdit->setEnabled(false);
        }
    } else {
        m_checkOutEdit->setTime(r.checkOut);
    }
    onTimeChanged();
}

void AttendanceDialog::onNoCheckOutToggled(bool checked) {
    m_checkOutEdit->setEnabled(!checked);
    onTimeChanged();
}

void AttendanceDialog::onTimeChanged() {
    bool isOpen = m_noCheckOut && m_noCheckOut->isChecked();
    if (isOpen) {
        m_hoursLabel->setText("--");
        m_wageLabel->setText("--");
        if (m_lateLabel) m_lateLabel->setText("--");
        return;
    }

    const QTime in  = m_checkInEdit->time();
    const QTime out = m_checkOutEdit->time();

    if (out <= in) {
        m_hoursLabel->setText("0.00");
        m_wageLabel->setText(CurrencyManager::format(0.0));
        if (m_lateLabel) m_lateLabel->setText("--");
        return;
    }

    AttendanceRecord preview;
    preview.checkIn  = in;
    preview.checkOut = out;

    if (m_employee.isMonthly()) {
        const DeductionPolicy::Mode dmode = DeductionPolicy::mode();
        const double penaltyPct = DeductionPolicy::perDayPenaltyPct();

        preview.calculateMonthly(
            m_employee.expectedCheckin, m_employee.expectedCheckout,
            m_employee.lateToleranceMin,
            m_employee.dailyRate(), m_employee.perMinuteRate(),
            dmode, penaltyPct);

        m_hoursLabel->setText(QString::number(preview.hoursWorked, 'f', 2));
        m_wageLabel->setText(CurrencyManager::format(preview.dailyWage));

        // Build deduction breakdown string — varies by mode
        if (m_lateLabel) {
            if (dmode == DeductionPolicy::Mode::Off) {
                m_lateLabel->setText(tr("Off — no automatic deduction"));
            } else if (dmode == DeductionPolicy::Mode::PerDay) {
                QStringList parts;
                if (preview.lateMinutes > 0) {
                    const double ded = m_employee.dailyRate() * penaltyPct / 100.0;
                    parts << tr("Late %1 min (-%2)").arg(preview.lateMinutes)
                                                    .arg(CurrencyManager::format(ded));
                }
                if (preview.earlyMinutes > 0) {
                    const double ded = m_employee.dailyRate() * penaltyPct / 100.0;
                    parts << tr("Early out %1 min (-%2)").arg(preview.earlyMinutes)
                                                         .arg(CurrencyManager::format(ded));
                }
                if (parts.isEmpty())
                    m_lateLabel->setText(tr("None — full daily rate"));
                else
                    m_lateLabel->setText(parts.join(", "));
            } else {
                // PerMinute — original behavior
                QStringList parts;
                const double base = m_employee.dailyRate();
                if (preview.lateMinutes > 0) {
                    const double ded = preview.lateMinutes * m_employee.perMinuteRate();
                    parts << tr("Late %1 min (-%2)").arg(preview.lateMinutes)
                                                    .arg(CurrencyManager::format(ded));
                }
                if (preview.earlyMinutes > 0) {
                    const double ded = preview.earlyMinutes * m_employee.perMinuteRate();
                    parts << tr("Early out %1 min (-%2)").arg(preview.earlyMinutes)
                                                         .arg(CurrencyManager::format(ded));
                }
                if (parts.isEmpty() && preview.dailyWage >= base - 0.001)
                    m_lateLabel->setText(tr("None — full daily rate"));
                else if (parts.isEmpty())
                    m_lateLabel->setText(tr("None"));
                else
                    m_lateLabel->setText(parts.join(", "));
            }
        }
    } else {
        preview.calculate(m_employee.hourlyWage);
        m_hoursLabel->setText(QString::number(preview.hoursWorked, 'f', 2));
        m_wageLabel->setText(CurrencyManager::format(preview.dailyWage));
    }
}

AttendanceRecord AttendanceDialog::record() const {
    AttendanceRecord r;
    r.id         = m_recordId;
    r.employeeId = m_employee.id;
    r.date       = m_dateEdit->date();
    r.checkIn    = m_checkInEdit->time();

    bool isOpen = m_noCheckOut && m_noCheckOut->isChecked();
    if (isOpen) {
        r.checkOut    = QTime();
        r.hoursWorked = 0.0;
        r.dailyWage   = 0.0;
    } else {
        r.checkOut = m_checkOutEdit->time();
        if (m_employee.isMonthly()) {
            r.calculateMonthly(
                m_employee.expectedCheckin, m_employee.expectedCheckout,
                m_employee.lateToleranceMin,
                m_employee.dailyRate(), m_employee.perMinuteRate(),
                DeductionPolicy::mode(),
                DeductionPolicy::perDayPenaltyPct());
        } else {
            r.calculate(m_employee.hourlyWage);
        }
    }
    return r;
}

bool AttendanceDialog::validate() {
    if (!m_dateEdit->date().isValid()) {
        QMessageBox::warning(this, tr("Validation"), tr("Please enter a valid date."));
        return false;
    }
    bool isOpen = m_noCheckOut && m_noCheckOut->isChecked();
    if (!isOpen && m_checkOutEdit->time() <= m_checkInEdit->time()) {
        QMessageBox::warning(this, tr("Validation"),
            tr("Check-out time must be after check-in time."));
        m_checkOutEdit->setFocus();
        return false;
    }
    return true;
}

void AttendanceDialog::onAccept() {
    if (validate()) accept();
}
