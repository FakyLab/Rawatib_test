#pragma once
#include <QDialog>
#include <QDateEdit>
#include <QTimeEdit>
#include <QLabel>
#include <QCheckBox>
#include "models/AttendanceRecord.h"
#include "models/Employee.h"

class AttendanceDialog : public QDialog {
    Q_OBJECT

public:
    // Add mode — pass full employee for correct wage calculation
    explicit AttendanceDialog(const Employee& employee, QWidget* parent = nullptr);
    // Edit mode — pass existing record + full employee
    explicit AttendanceDialog(const AttendanceRecord& record,
                              const Employee& employee, QWidget* parent = nullptr);

    AttendanceRecord record() const;

private slots:
    void onTimeChanged();
    void onAccept();
    void onNoCheckOutToggled(bool checked);

private:
    void setupUi();
    void populate(const AttendanceRecord& record);
    bool validate();

    QDateEdit*  m_dateEdit     = nullptr;
    QTimeEdit*  m_checkInEdit  = nullptr;
    QTimeEdit*  m_checkOutEdit = nullptr;
    QCheckBox*  m_noCheckOut   = nullptr;
    QLabel*     m_hoursLabel   = nullptr;
    QLabel*     m_wageLabel    = nullptr;
    QLabel*     m_lateLabel    = nullptr;   // monthly only

    Employee m_employee;
    int      m_recordId  = 0;
    bool     m_isEditMode = false;
};
