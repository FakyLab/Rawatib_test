#pragma once
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>

// ── ExportAllDialog ───────────────────────────────────────────────────────
//
// Shown by File → Export All Employees... (Ctrl+Shift+E).
// Collects two user choices before the save-file dialog appears:
//
//   Period — a specific month/year, or All Time
//   Format — CSV (re-importable) or XLSX (Excel report)
//
// The format choice is surfaced here rather than in the Save As dialog
// because CSV and XLSX serve fundamentally different purposes for bulk
// export: CSV is used to re-import all employees into another instance
// of Rawatib; XLSX is for human review in Excel. Making this distinction
// visible helps users pick the right format intentionally.
//
// Usage:
//   ExportAllDialog dlg(parent);
//   if (dlg.exec() != QDialog::Accepted) return;
//   const bool allTime = dlg.isAllTime();
//   const int  month   = dlg.selectedMonth();   // 1–12; 0 if allTime
//   const int  year    = dlg.selectedYear();    // e.g. 2025; 0 if allTime
//   const bool csv     = dlg.isCsv();

class ExportAllDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportAllDialog(QWidget* parent = nullptr);

    // Returns true when the user selected "All Time"
    bool isAllTime() const;

    // Returns selected month (1–12). Returns 0 when isAllTime() is true.
    int selectedMonth() const;

    // Returns selected year (e.g. 2025). Returns 0 when isAllTime() is true.
    int selectedYear() const;

    // Returns true when the user selected CSV format.
    // Returns false for XLSX.
    bool isCsv() const;

private slots:
    void onAllTimeToggled(bool checked);

private:
    QComboBox*   m_monthCombo    = nullptr;
    QComboBox*   m_yearCombo     = nullptr;
    QCheckBox*   m_allTimeCheck  = nullptr;
    QRadioButton* m_csvRadio     = nullptr;
    QRadioButton* m_xlsxRadio    = nullptr;
};
