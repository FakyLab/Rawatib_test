#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QPushButton>

class AuditLogDialog : public QDialog {
    Q_OBJECT

public:
    explicit AuditLogDialog(QWidget* parent = nullptr);

private slots:
    void loadEntries();
    void onVerify();
    void onExport();

private:
    void setupUi();

    QComboBox*    m_actionCombo  = nullptr;
    QDateEdit*    m_fromDate     = nullptr;
    QDateEdit*    m_toDate       = nullptr;
    QTableWidget* m_table        = nullptr;
    QLabel*       m_statusLabel  = nullptr;
    QPushButton*  m_verifyBtn    = nullptr;
    QPushButton*  m_exportBtn    = nullptr;
};
