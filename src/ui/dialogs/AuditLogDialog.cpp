#include "ui/dialogs/AuditLogDialog.h"
#include "utils/AuditLog.h"
#include "database/DatabaseManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QFile>
#include <QDate>
#include <QApplication>

AuditLogDialog::AuditLogDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Audit Log"));
    setMinimumSize(700, 480);
    resize(960, 620);
    setSizeGripEnabled(true);
    setupUi();
    loadEntries();
}

void AuditLogDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 10);

    // ── Filter row ────────────────────────────────────────────────────────
    auto* filterRow = new QHBoxLayout();

    filterRow->addWidget(new QLabel(tr("Action:"), this));
    m_actionCombo = new QComboBox(this);
    m_actionCombo->addItem(tr("All actions"),         QString());
    m_actionCombo->addItem(tr("Employee changes"),    QString("employee"));
    m_actionCombo->addItem(tr("Attendance changes"),  QString("attendance_group"));
    m_actionCombo->addItem(tr("Payments"),            QString("payment_group"));
    m_actionCombo->addItem(tr("Import"),              QString(AuditLog::IMPORT));
    m_actionCombo->addItem(tr("PIN / Security"),      QString("security_group"));
    m_actionCombo->addItem(tr("Lock policy"),         QString(AuditLog::LOCK_POLICY));
    m_actionCombo->addItem(tr("Backup / Restore"),    QString("backup_group"));
    filterRow->addWidget(m_actionCombo);

    filterRow->addSpacing(12);
    filterRow->addWidget(new QLabel(tr("From:"), this));
    m_fromDate = new QDateEdit(QDate::currentDate().addMonths(-1), this);
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setDisplayFormat("yyyy-MM-dd");
    filterRow->addWidget(m_fromDate);

    filterRow->addWidget(new QLabel(tr("To:"), this));
    m_toDate = new QDateEdit(QDate::currentDate(), this);
    m_toDate->setCalendarPopup(true);
    m_toDate->setDisplayFormat("yyyy-MM-dd");
    filterRow->addWidget(m_toDate);

    // Responsive filtering — reloads immediately on any change
    connect(m_actionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AuditLogDialog::loadEntries);
    connect(m_fromDate, &QDateEdit::dateChanged,
            this, &AuditLogDialog::loadEntries);
    connect(m_toDate, &QDateEdit::dateChanged,
            this, &AuditLogDialog::loadEntries);

    filterRow->addStretch();

    m_verifyBtn = new QPushButton(tr("Verify Integrity"), this);
    connect(m_verifyBtn, &QPushButton::clicked, this, &AuditLogDialog::onVerify);
    filterRow->addWidget(m_verifyBtn);

    mainLayout->addLayout(filterRow);

    // ── Table ─────────────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        tr("Timestamp"), tr("Action"), tr("Entity"),
        tr("Detail"), tr("Old Value"), tr("New Value"), tr("ID")
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_table, 1);

    // ── Status + bottom row ───────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_exportBtn = new QPushButton(tr("Export to CSV"), this);
    connect(m_exportBtn, &QPushButton::clicked, this, &AuditLogDialog::onExport);
    bottomRow->addWidget(m_exportBtn);
    bottomRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);
}

void AuditLogDialog::loadEntries() {
    const QString fromStr = m_fromDate->date().toString(Qt::ISODate);
    const QString toStr   = m_toDate->date().addDays(1).toString(Qt::ISODate);
    const QString actionFilter = m_actionCombo->currentData().toString();

    // Build WHERE clause
    QString where = "WHERE timestamp >= :from AND timestamp < :to";

    // Action group filters
    QStringList actionList;
    if (actionFilter == "employee") {
        actionList = { AuditLog::ADD_EMPLOYEE, AuditLog::EDIT_EMPLOYEE,
                       AuditLog::DELETE_EMPLOYEE };
    } else if (actionFilter == "attendance_group") {
        actionList = { AuditLog::ADD_ATTENDANCE, AuditLog::EDIT_ATTENDANCE,
                       AuditLog::DELETE_ATTENDANCE };
    } else if (actionFilter == "payment_group") {
        actionList = { AuditLog::MARK_PAID, AuditLog::MARK_UNPAID,
                       AuditLog::PAY_MONTH };
    } else if (actionFilter == "security_group") {
        actionList = { AuditLog::PIN_SET, AuditLog::PIN_CHANGED,
                       AuditLog::PIN_REMOVED, AuditLog::BYPASS_USED };
    } else if (actionFilter == "backup_group") {
        actionList = { AuditLog::BACKUP, AuditLog::RESTORE };
    } else if (!actionFilter.isEmpty()) {
        actionList = { actionFilter };
    }

    if (!actionList.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < actionList.size(); ++i)
            placeholders << QString(":act%1").arg(i);
        where += QString(" AND action IN (%1)").arg(placeholders.join(","));
    }

    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QString(
        "SELECT id, timestamp, action, entity, entity_id, "
        "detail, old_value, new_value "
        "FROM audit_log %1 ORDER BY id DESC").arg(where));
    q.bindValue(":from", fromStr);
    q.bindValue(":to",   toStr);
    for (int i = 0; i < actionList.size(); ++i)
        q.bindValue(QString(":act%1").arg(i), actionList[i]);

    m_table->setRowCount(0);

    if (!q.exec()) {
        m_statusLabel->setText(tr("Query failed: %1").arg(q.lastError().text()));
        return;
    }

    int row = 0;
    while (q.next()) {
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(q.value(1).toString()));
        m_table->setItem(row, 1, new QTableWidgetItem(q.value(2).toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(q.value(3).toString()));
        m_table->setItem(row, 3, new QTableWidgetItem(q.value(5).toString()));
        m_table->setItem(row, 4, new QTableWidgetItem(q.value(6).toString()));
        m_table->setItem(row, 5, new QTableWidgetItem(q.value(7).toString()));
        m_table->setItem(row, 6, new QTableWidgetItem(QString::number(q.value(0).toInt())));
        ++row;
    }

    m_statusLabel->setText(tr("%1 entries").arg(row));
}

void AuditLogDialog::onVerify() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int brokenAtId = -1;
    const bool ok = AuditLog::verify(brokenAtId);
    QApplication::restoreOverrideCursor();

    if (ok) {
        m_statusLabel->setStyleSheet("color: #1B5E20; font-weight: bold;");
        m_statusLabel->setText(tr("✓ Log integrity verified — no tampering detected."));
    } else {
        m_statusLabel->setStyleSheet("color: #B71C1C; font-weight: bold;");
        m_statusLabel->setText(tr("✗ Tampering detected — chain broken at entry #%1. "
                                   "Records after this point may have been modified or deleted.")
                                   .arg(brokenAtId));
    }
}

void AuditLogDialog::onExport() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Audit Log"),
        QDir::homePath() + "/audit_log.csv",
        tr("CSV File (*.csv);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export Failed"),
            tr("Could not write to:\n%1").arg(path));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "\xEF\xBB\xBF";  // UTF-8 BOM for Excel

    // Header
    out << "Timestamp,Action,Entity,Detail,Old Value,New Value,ID\n";

    // Rows from current table view
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList cols;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QString val = m_table->item(r, c) ? m_table->item(r, c)->text() : QString();
            // Escape quotes
            val.replace("\"", "\"\"");
            cols << "\"" + val + "\"";
        }
        out << cols.join(",") << "\n";
    }

    file.close();
    QMessageBox::information(this, tr("Export Successful"),
        tr("Audit log exported to:\n%1").arg(path));
}
