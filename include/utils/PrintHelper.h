#pragma once
#include "models/AttendanceRecord.h"
#include "repositories/AttendanceRepository.h"
#include "utils/PayrollCalculator.h"
#include <QString>
#include <QVector>
#include <QWidget>

namespace PrintHelper {

// ── Public print functions ─────────────────────────────────────────────────

// payrollResult: pass a valid Result when payroll rules are enabled.
// Pass a default-constructed Result (grossPay == 0) to omit net pay section.
void printMonthlyReport(const QString& employeeName,
                        int employeeId,
                        int month, int year,
                        const PayrollCalculator::Result& payrollResult,
                        QWidget* parent,
                        bool wagesVisible = true);

void printPaymentSlip(const QString& employeeName,
                      int month, int year,
                      double totalSalary,
                      double previouslyPaid,
                      double amountPaidNow,
                      QWidget* parent);

// ── Internal HTML builders ─────────────────────────────────────────────────

void appendSummaryRows(QString& html,
                       const MonthlySummary& s,
                       bool receipt,
                       bool highlight,
                       bool wagesVisible = true);

void appendNetPayRows(QString& html,
                      const PayrollCalculator::Result& result,
                      bool receipt);

QString buildA4Html(const QString& employeeName,
                    int month, int year,
                    const MonthlySummary& s,
                    const QVector<AttendanceRecord>& records,
                    const PayrollCalculator::Result& payrollResult,
                    bool wagesVisible = true);

QString buildReceiptHtml(const QString& employeeName,
                         int month, int year,
                         const MonthlySummary& s,
                         const QVector<AttendanceRecord>& records,
                         const PayrollCalculator::Result& payrollResult,
                         int bodyPt, int smallPt,
                         bool wagesVisible = true);

QString buildPaymentSlipHtml(const QString& employeeName,
                              int month, int year,
                              double totalSalary,
                              double previouslyPaid,
                              double amountPaidNow);

QString buildPaymentSlipReceiptHtml(const QString& employeeName,
                                     int month, int year,
                                     double totalSalary,
                                     double previouslyPaid,
                                     double amountPaidNow,
                                     int bodyPt, int smallPt);

} // namespace PrintHelper
