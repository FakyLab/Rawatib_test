#pragma once
#include <QDialog>
#include <QCheckBox>
#include <QLabel>

class PayMonthDialog : public QDialog {
    Q_OBJECT

public:
    explicit PayMonthDialog(const QString& employeeName,
                            int month, int year,
                            QWidget* parent = nullptr);

    bool printRequested() const;

private:
    QCheckBox* m_printCheck = nullptr;
};
