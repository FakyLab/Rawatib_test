#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include "models/Employee.h"
#include "utils/LockPolicy.h"

class EmployeePanel : public QWidget {
    Q_OBJECT

public:
    explicit EmployeePanel(QWidget* parent = nullptr);
    void refreshList();

signals:
    void employeeSelected(int employeeId);
    void employeeListChanged();
    void adminUnlocked();   // emitted after successful inline unlock

public slots:
    void onLockChanged(bool unlocked);

private slots:
    void onAddEmployee();
    void onEditEmployee();
    void onDeleteEmployee();
    void onSearchChanged(const QString& text);
    void onItemSelectionChanged();

private:
    void setupUi();
    int currentEmployeeId() const;
    bool guardAdmin(LockPolicy::Feature feature);

    QLineEdit*   m_searchEdit  = nullptr;
    QListWidget* m_listWidget  = nullptr;
    QPushButton* m_addBtn      = nullptr;
    QPushButton* m_editBtn     = nullptr;
    QPushButton* m_deleteBtn   = nullptr;

    bool m_adminUnlocked = true;  // mirrors MainWindow state
};
