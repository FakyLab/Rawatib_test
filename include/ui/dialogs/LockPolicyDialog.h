#pragma once
#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include "utils/LockPolicy.h"

class LockPolicyDialog : public QDialog {
    Q_OBJECT

public:
    explicit LockPolicyDialog(QWidget* parent = nullptr);

private slots:
    void onFeatureToggled(LockPolicy::Feature feature, bool locked);
    void onGroupToggled(int groupIndex, Qt::CheckState state);
    void onRestoreDefaults();

private:
    void setupUi();

    struct FeatureRow {
        LockPolicy::Feature feature;
        QCheckBox*          check = nullptr;
    };

    struct Group {
        QCheckBox*           masterCheck = nullptr;
        QVector<FeatureRow>  rows;
        bool                 updating = false;
    };

    void updateGroupMaster(Group& g);
    void updatePinToggles();   // grey out PIN feature checkboxes when prereqs not met

    QVector<Group> m_groups;
    QCheckBox*     m_employeePinCheck  = nullptr;  // mark-paid feature toggle
    QCheckBox*     m_kioskPinCheck     = nullptr;  // check-in/out feature toggle
};
