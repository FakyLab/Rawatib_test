#include "ui/EmployeePanel.h"
#include "ui/dialogs/EmployeeDialog.h"
#include "ui/dialogs/PinDialog.h"
#include "repositories/EmployeeRepository.h"
#include "utils/PinManager.h"
#include "utils/CurrencyManager.h"
#include "utils/LockPolicy.h"
#include "utils/EmployeePinManager.h"
#include "utils/AuditLog.h"
#include "utils/SessionManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QListWidgetItem>
#include <algorithm>

EmployeePanel::EmployeePanel(QWidget* parent) : QWidget(parent) {
    // Start unlocked if no PIN set
    m_adminUnlocked = !PinManager::isPinSet();
    setupUi();
    refreshList();
}

bool EmployeePanel::guardAdmin(LockPolicy::Feature feature) {
    if (!LockPolicy::isLocked(feature)) return true;
    if (SessionManager::isUnlocked()) return true;
    // Inline unlock — prompt right here, no need to find the lock icon
    if (PinDialog::requestUnlock(this)) {
        SessionManager::setUnlocked(true);
        emit adminUnlocked();
        return true;
    }
    return false;
}

void EmployeePanel::setupUi() {
    setMinimumWidth(200);
    setMaximumWidth(280);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search employees..."));
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget, 1);

    // Buttons row
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);

    m_addBtn    = new QPushButton(tr("Add"),    this);
    m_editBtn   = new QPushButton(tr("Edit"),   this);
    m_deleteBtn = new QPushButton(tr("Delete"), this);

    m_editBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_editBtn);
    btnLayout->addWidget(m_deleteBtn);
    layout->addLayout(btnLayout);

    connect(m_addBtn,    &QPushButton::clicked, this, &EmployeePanel::onAddEmployee);
    connect(m_editBtn,   &QPushButton::clicked, this, &EmployeePanel::onEditEmployee);
    connect(m_deleteBtn, &QPushButton::clicked, this, &EmployeePanel::onDeleteEmployee);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &EmployeePanel::onSearchChanged);
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, &EmployeePanel::onItemSelectionChanged);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &EmployeePanel::onEditEmployee);
}

void EmployeePanel::refreshList() {
    const QString searchText = m_searchEdit ? m_searchEdit->text() : QString();
    onSearchChanged(searchText);
}

void EmployeePanel::onSearchChanged(const QString& text) {
    auto& repo = EmployeeRepository::instance();
    QVector<Employee> employees = text.trimmed().isEmpty()
        ? repo.getAllEmployees()
        : repo.searchEmployees(text);

    // Sort by name using the OS locale — handles Arabic, Latin, and mixed
    // lists correctly. QString::localeAwareCompare() uses the platform's
    // native collation (CompareStringEx on Windows, ICU on Linux/macOS),
    // which SQLite's default binary ORDER BY cannot do.
    std::sort(employees.begin(), employees.end(),
        [](const Employee& a, const Employee& b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });

    m_listWidget->clear();
    const bool hideW = LockPolicy::isLocked(LockPolicy::Feature::HideWages)
                    && !m_adminUnlocked;
    for (const auto& emp : employees) {
        QString label;
        if (hideW) {
            label = emp.name;
        } else if (emp.isMonthly()) {
            label = QString("%1  (%2 %3)").arg(emp.name)
                        .arg(CurrencyManager::format(emp.monthlySalary))
                        .arg(tr("/month"));
        } else {
            label = QString("%1  (%2 %3)").arg(emp.name)
                        .arg(emp.hourlyWage, 0, 'f', 2)
                        .arg(CurrencyManager::symbol() + tr("/hr"));
        }
        auto* item = new QListWidgetItem(label, m_listWidget);
        item->setData(Qt::UserRole, emp.id);
        m_listWidget->addItem(item);
    }
}

int EmployeePanel::currentEmployeeId() const {
    auto* item = m_listWidget->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void EmployeePanel::onLockChanged(bool unlocked) {
    m_adminUnlocked = unlocked;
    refreshList();
}

void EmployeePanel::onItemSelectionChanged() {
    int id = currentEmployeeId();
    bool has = id > 0;
    m_editBtn->setEnabled(has);
    m_deleteBtn->setEnabled(has);
    if (has) emit employeeSelected(id);
}

void EmployeePanel::onAddEmployee() {
    if (!guardAdmin(LockPolicy::Feature::AddEmployee)) return;
    EmployeeDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Employee emp = dlg.employee();
        if (EmployeeRepository::instance().addEmployee(emp)) {
            if (dlg.pinAction() == EmployeeDialog::PinAction::SetNew)
                EmployeeRepository::instance().setEmployeePin(emp.id, dlg.newPin());
            AuditLog::record(AuditLog::ADD_EMPLOYEE, "employee", emp.id,
                emp.isMonthly()
                    ? QString("Added employee \"%1\" monthly=%2").arg(emp.name).arg(emp.monthlySalary, 0, 'f', 2)
                    : QString("Added employee \"%1\" wage=%2").arg(emp.name).arg(emp.hourlyWage, 0, 'f', 2));
            refreshList();
            emit employeeListChanged();
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("Failed to add employee:\n%1").arg(EmployeeRepository::instance().lastError()));
        }
    }
}

void EmployeePanel::onEditEmployee() {
    if (!guardAdmin(LockPolicy::Feature::EditEmployee)) return;
    int id = currentEmployeeId();
    if (id <= 0) return;
    auto emp = EmployeeRepository::instance().getEmployee(id);
    if (!emp) return;
    EmployeeDialog dlg(*emp, this);
    if (dlg.exec() == QDialog::Accepted) {
        Employee updated = dlg.employee();
        if (EmployeeRepository::instance().updateEmployee(updated)) {
            if (dlg.pinAction() == EmployeeDialog::PinAction::SetNew)
                EmployeeRepository::instance().setEmployeePin(id, dlg.newPin());
            else if (dlg.pinAction() == EmployeeDialog::PinAction::Clear)
                EmployeeRepository::instance().clearEmployeePin(id);
            AuditLog::record(AuditLog::EDIT_EMPLOYEE, "employee", id,
                QString("Edited employee \"%1\"").arg(updated.name),
                emp->isMonthly()
                    ? QString("name=%1 monthly=%2 phone=%3").arg(emp->name).arg(emp->monthlySalary, 0,'f',2).arg(emp->phone)
                    : QString("name=%1 wage=%2 phone=%3").arg(emp->name).arg(emp->hourlyWage, 0,'f',2).arg(emp->phone),
                updated.isMonthly()
                    ? QString("name=%1 monthly=%2 phone=%3").arg(updated.name).arg(updated.monthlySalary, 0,'f',2).arg(updated.phone)
                    : QString("name=%1 wage=%2 phone=%3").arg(updated.name).arg(updated.hourlyWage, 0,'f',2).arg(updated.phone));
            refreshList();
            emit employeeListChanged();
            emit employeeSelected(id);
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("Failed to update employee:\n%1").arg(EmployeeRepository::instance().lastError()));
        }
    }
}

void EmployeePanel::onDeleteEmployee() {
    if (!guardAdmin(LockPolicy::Feature::DeleteEmployee)) return;
    int id = currentEmployeeId();
    if (id <= 0) return;
    auto emp = EmployeeRepository::instance().getEmployee(id);
    if (!emp) return;
    auto reply = QMessageBox::question(this, tr("Confirm Delete"),
        tr("Delete employee '%1'?\nAll attendance records will also be deleted.").arg(emp->name),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (EmployeeRepository::instance().deleteEmployee(id)) {
            AuditLog::record(AuditLog::DELETE_EMPLOYEE, "employee", id,
                QString("Deleted employee \"%1\"").arg(emp->name));
            refreshList();
            emit employeeListChanged();
            emit employeeSelected(-1);
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("Failed to delete employee:\n%1").arg(EmployeeRepository::instance().lastError()));
        }
    }
}
