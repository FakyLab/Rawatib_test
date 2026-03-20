#pragma once
#include "utils/ImportHelper.h"
#include <QDialog>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QGroupBox>
#include <QVBoxLayout>

// ── ImportPreviewDialog ────────────────────────────────────────────────────
//
// Shown only when pass 1 detects any issues (conflicts, unknown employees,
// wage mismatches, checksum failures).
//
// Displays a row-level preview table with checkboxes. The admin can:
//   - Check/uncheck individual records to include or exclude them
//   - Choose wage resolution per employee (for wage-mismatch employees)
//   - Choose create-new vs skip for unknown employees
//   - Filter the table to show only conflict rows
//
// When the admin clicks "Import Selected", the dialog closes with Accepted
// and pass1 reflects the admin's decisions (selected flags + wage decisions).
//
// The dialog does NOT write to the DB — it only mutates the ParsePass1Result.

class ImportPreviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportPreviewDialog(ImportHelper::ParsePass1Result& pass1,
                                  QWidget* parent = nullptr);

private slots:
    void onFilterToggled(bool conflictsOnly);
    void onSelectAllToggled(bool checked);
    void onImportClicked();
    void onRowCheckChanged(int row, int col);
    void onWageDecisionChanged(int empIndex, int decision);
    void onEmployeeResolutionChanged(int empIndex, int decision);
    void updateImportButton();

private:
    void setupUi();
    void buildSummaryHeader();
    void buildEmployeeResolutionBoxes();
    void buildTable();
    void populateTable(bool conflictsOnly);
    void rebuildTable(bool conflictsOnly);

    // Maps table row → (employeeIndex, recordIndex) for checkbox sync
    struct RowMapping {
        int empIndex = -1;
        int recIndex = -1;
    };

    ImportHelper::ParsePass1Result& m_pass1;

    // UI elements
    QLabel*       m_summaryLabel     = nullptr;
    QWidget*      m_resolutionArea   = nullptr;   // per-employee resolution boxes
    QCheckBox*    m_filterCheck      = nullptr;
    QCheckBox*    m_selectAllCheck   = nullptr;
    QTableWidget* m_table            = nullptr;
    QPushButton*  m_importBtn        = nullptr;
    QPushButton*  m_cancelBtn        = nullptr;

    QVector<RowMapping> m_rowMap;   // table row index → pass1 indices
    bool m_updatingChecks = false;  // guard against recursive checkbox signals
};
