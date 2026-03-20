#include "utils/PrintHelper.h"
#include "utils/CurrencyManager.h"
#include "utils/SettingsManager.h"
#include "utils/PayrollCalculator.h"
#include "utils/DeductionPolicy.h"
#include "ui/AdvancedTab.h"
#include <cmath>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QPageSize>
#include <QPageLayout>
#include <QDate>
#include <QLocale>
#include <QCoreApplication>
#include <QGuiApplication>

// Translation shorthand — context matches <context><n> in rawatib_ar.ts
static inline QString tr(const char* key) {
    return QCoreApplication::translate("PrintHelper", key);
}

static inline bool isRtl() {
    return QGuiApplication::layoutDirection() == Qt::RightToLeft;
}

// Returns the QLocale matching the active UI language so that month names
// in print reports appear in the user's chosen language (e.g. "janvier" in
// French, "Ocak" in Turkish).  Falls back to QLocale::c() (English) for
// any language not explicitly mapped — safe for future additions.
static inline QLocale appLocale() {
    const QString lang = SettingsManager::getLanguage();
    if (lang == "ar") return QLocale(QLocale::Arabic);
    if (lang == "fr") return QLocale(QLocale::French);
    if (lang == "tr") return QLocale(QLocale::Turkish);
    if (lang == "zh") return QLocale(QLocale::Chinese);
    if (lang == "ja") return QLocale(QLocale::Japanese);
    if (lang == "ko") return QLocale(QLocale::Korean);
    return QLocale::c();   // English / any unmapped language
}

// ── CSS font stacks ────────────────────────────────────────────────────────
//
// Arial and Georgia have no CJK glyph coverage — using them alone for
// Chinese, Japanese, or Korean produces blank boxes (tofu) in print output.
//
// The stacks below are ordered: Windows system font → macOS system font →
// Noto (cross-platform/Linux) → generic fallback.  Qt's CSS engine picks
// the first font in the list that is installed on the machine, so any one
// of the platform-specific entries being present is sufficient.
//
// sansStack() — used for body text and UI labels.
// serifStack() — used for decorative headings (report title, slip title,
//                amount value).  For CJK the serif/sans distinction is
//                less meaningful; we fall back to the CJK sans font rather
//                than risking a serif font with no CJK coverage.

static inline QString sansStack() {
    const QString lang = SettingsManager::getLanguage();
    if (lang == "zh")
        // Simplified Chinese: Microsoft YaHei (Win), PingFang SC (macOS),
        // Noto Sans CJK SC (Linux)
        return QStringLiteral("Microsoft YaHei, PingFang SC, "
                              "Noto Sans CJK SC, Arial, sans-serif");
    if (lang == "ja")
        // Japanese: Meiryo (Win), Yu Gothic (Win10+), Hiragino Sans (macOS),
        // Noto Sans CJK JP (Linux)
        return QStringLiteral("Meiryo, Yu Gothic, Hiragino Sans, "
                              "Noto Sans CJK JP, Arial, sans-serif");
    if (lang == "ko")
        // Korean: Malgun Gothic (Win), Apple SD Gothic Neo (macOS),
        // Noto Sans CJK KR (Linux)
        return QStringLiteral("Malgun Gothic, Apple SD Gothic Neo, "
                              "Noto Sans CJK KR, Arial, sans-serif");
    // Arabic, Latin (en/fr/tr), and any future language:
    // Arial has good Arabic coverage; Tahoma is a common Arabic fallback on
    // older Windows.  For Latin languages this is identical to before.
    return QStringLiteral("Arial, Tahoma, sans-serif");
}

static inline QString serifStack() {
    const QString lang = SettingsManager::getLanguage();
    if (lang == "zh")
        // SimSun (Win), Songti SC (macOS), Noto Serif CJK SC (Linux)
        return QStringLiteral("SimSun, Songti SC, "
                              "Noto Serif CJK SC, serif");
    if (lang == "ja")
        // MS Mincho (Win), Yu Mincho (Win8+), Hiragino Mincho ProN (macOS),
        // Noto Serif CJK JP (Linux)
        return QStringLiteral("MS Mincho, Yu Mincho, Hiragino Mincho ProN, "
                              "Noto Serif CJK JP, serif");
    if (lang == "ko")
        // Batang (Win), Apple Myungjo (macOS), Noto Serif CJK KR (Linux)
        return QStringLiteral("Batang, Apple Myungjo, "
                              "Noto Serif CJK KR, serif");
    // Arabic and Latin: Georgia has Arabic fallback via system fonts;
    // for robustness we add Arial which has solid Arabic coverage.
    return QStringLiteral("Georgia, Arial, serif");
}

// Returns "right" or "left" for the primary text alignment
static inline QString pAlign() { return isRtl() ? "right" : "left";  }
// Returns the opposite edge — for values that sit on the far side
static inline QString sAlign() { return isRtl() ? "left"  : "right"; }

namespace PrintHelper {

// ── appendSummaryRows ──────────────────────────────────────────────────────
// Receipt variant: plain <p> lines, label and value on same line.
// A4 variant: two-column table — label column on primary side, value opposite.
// RTL: label is right-aligned, value is left-aligned, and we swap column order
//      in the table so the label column is physically on the right.
void appendSummaryRows(QString& html, const MonthlySummary& s,
                       bool receipt, bool highlight,
                       bool wagesVisible)
{
    const bool rtl = isRtl();

    if (receipt) {
        auto line = [&](const QString& label, const QString& value,
                        bool bold = false) {
            const QString content = bold
                ? QString("<b>%1:</b> %2").arg(label).arg(value)
                : QString("%1: %2").arg(label).arg(value);
            html += QString("<p style='margin:2px 0; text-align:%1;'>%2</p>")
                        .arg(pAlign()).arg(content);
        };
        if (s.isMonthly) {
            line(tr("Days Present"), QString::number(s.presentDays));
            line(tr("Days Absent"),  QString::number(s.absentDays));
        } else {
            line(tr("Days"),  QString::number(s.totalDays));
            line(tr("Hours"), QString("%1 hrs").arg(s.totalHours, 0,'f',2));
        }
        if (wagesVisible) {
            if (s.isMonthly)
                line(tr("Expected"), CurrencyManager::formatHtml(s.expectedSalary));
            line(tr("Salary"), CurrencyManager::formatHtml(s.totalSalary));
            if (s.isMonthly && s.totalDeductions > 0)
                line(tr("Absent Ded."), CurrencyManager::formatHtml(s.absentDeduction));
            if (s.isMonthly && s.lateDeduction > 0)
                line(tr("Late Ded."),  CurrencyManager::formatHtml(s.lateDeduction));
            if (s.isMonthly && s.earlyDeduction > 0)
                line(tr("Early Ded."), CurrencyManager::formatHtml(s.earlyDeduction));
            line(tr("Paid"),   CurrencyManager::formatHtml(s.paidAmount));
            line(tr("Unpaid"), CurrencyManager::formatHtml(s.unpaidAmount), highlight);
        }
        return;
    }

    auto row = [&](const QString& label, const QString& value, bool hl = false) {
        const QString cls = hl ? " class='summary-highlight'" : "";
        if (rtl) {
            html += QString("<tr%1>"
                            "<td class='summary-value'>%2</td>"
                            "<td class='summary-label'>%3</td>"
                            "</tr>").arg(cls).arg(value).arg(label);
        } else {
            html += QString("<tr%1>"
                            "<td class='summary-label'>%2</td>"
                            "<td class='summary-value'>%3</td>"
                            "</tr>").arg(cls).arg(label).arg(value);
        }
    };

    if (s.isMonthly) {
        row(tr("Days Present"),  QString("<b>%1</b> days").arg(s.presentDays));
        row(tr("Days Absent"),   QString("<b>%1</b> days").arg(s.absentDays));
    } else {
        row(tr("Total Days Worked"),  QString("<b>%1</b> days").arg(s.totalDays));
        row(tr("Total Hours Worked"), QString("<b>%1</b> hrs") .arg(s.totalHours, 0,'f',2));
    }
    if (wagesVisible) {
        if (s.isMonthly) {
            row(tr("Expected Salary"),  "<b>" + CurrencyManager::formatHtml(s.expectedSalary)   + "</b>");
            row(tr("Absent Deduction"), "<b>" + CurrencyManager::formatHtml(s.absentDeduction)  + "</b>");
            if (s.lateDeduction > 0)
                row(tr("Late Deduction"),
                    QString("<b>%1</b>  <span style='color:gray;font-size:90%%'>(%2 min)</span>")
                        .arg(CurrencyManager::formatHtml(s.lateDeduction))
                        .arg(s.totalLateMinutes));
            if (s.earlyDeduction > 0)
                row(tr("Early Deduction"),
                    QString("<b>%1</b>  <span style='color:gray;font-size:90%%'>(%2 min)</span>")
                        .arg(CurrencyManager::formatHtml(s.earlyDeduction))
                        .arg(s.totalEarlyMinutes));
        }
        row(tr("Total Salary"),     "<b>" + CurrencyManager::formatHtml(s.totalSalary)  + "</b>");
        row(tr("Paid Amount"),      "<b>" + CurrencyManager::formatHtml(s.paidAmount)   + "</b>");
        row(tr("Unpaid Remaining"), "<b>" + CurrencyManager::formatHtml(s.unpaidAmount) + "</b>", highlight);
    }
}

// ── appendNetPayRows ───────────────────────────────────────────────────────
// Appended after appendSummaryRows when payroll rules are enabled.
// Shows each deduction/addition line item and the final net pay.
// Only called when result.grossPay > 0 (i.e. rules are active and there is data).

void appendNetPayRows(QString& html,
                      const PayrollCalculator::Result& result,
                      bool receipt)
{
    if (result.breakdown.isEmpty() && result.grossPay == 0.0) return;

    const bool rtl = isRtl();
    const QString pa = pAlign();
    const QString sa = sAlign();

    if (receipt) {
        html += QString("<p style='margin:6px 0 2px 0; text-align:%1; "
                        "font-weight:bold; border-top:1px solid #ccc; "
                        "padding-top:4px;'>%2</p>")
                    .arg(pa).arg(tr("Payroll Adjustments"));
        for (const auto& item : result.breakdown) {
            const bool isDeduction = item.amount < 0;
            const QString sign     = isDeduction ? "−" : "+";
            const QString color    = isDeduction ? "#c0392b" : "#1e8449";
            html += QString("<p style='margin:2px 0; text-align:%1; color:%2;'>"
                            "%3: %4 %5</p>")
                        .arg(pa).arg(color)
                        .arg(item.name)
                        .arg(sign)
                        .arg(CurrencyManager::formatHtml(std::abs(item.amount)));
        }
        html += QString("<p style='margin:4px 0 2px 0; text-align:%1; "
                        "font-weight:bold; font-size:11pt; color:#1a1a1a; "
                        "border-top:1px solid #333;'>"
                        "<b>%2:</b> %3</p>")
                    .arg(pa).arg(tr("Net Pay"))
                    .arg(CurrencyManager::formatHtml(result.netPay));
        return;
    }

    // A4 two-column table rows
    auto row = [&](const QString& label, const QString& value,
                   const QString& color = QString()) {
        const QString style = color.isEmpty()
            ? QString()
            : QString(" style='color:%1;'").arg(color);
        if (rtl) {
            html += QString("<tr><td class='summary-value'%1>%2</td>"
                            "<td class='summary-label'%1>%3</td></tr>")
                        .arg(style).arg(value).arg(label);
        } else {
            html += QString("<tr><td class='summary-label'%1>%2</td>"
                            "<td class='summary-value'%1>%3</td></tr>")
                        .arg(style).arg(label).arg(value);
        }
    };

    // Section divider row
    html += QString("<tr><td colspan='2' style='padding:6px 14px 2px; "
                    "font-weight:bold; border-top:2px solid #bbb; "
                    "text-align:%1; color:#555;'>%2</td></tr>")
                .arg(pa).arg(tr("Payroll Adjustments"));

    for (const auto& item : result.breakdown) {
        const bool isDeduction = item.amount < 0;
        const QString sign     = isDeduction ? "−&nbsp;" : "+&nbsp;";
        const QString color    = isDeduction ? "#c0392b" : "#1e8449";
        row(item.name,
            "<b>" + sign + CurrencyManager::formatHtml(std::abs(item.amount)) + "</b>",
            color);
    }

    // Net pay highlight row
    html += QString("<tr class='summary-highlight'>"
                    "<td colspan='2' style='text-align:center; "
                    "font-size:12pt; font-weight:bold; padding:6px 14px; "
                    "color:#1a1a1a; border-top:2px solid #333;'>"
                    "%1: %2</td></tr>")
                .arg(tr("Net Pay"))
                .arg(CurrencyManager::formatHtml(result.netPay));
}

// ── buildA4Html ────────────────────────────────────────────────────────────
QString buildA4Html(const QString& employeeName, int month, int year,
                    const MonthlySummary& s,
                    const QVector<AttendanceRecord>& records,
                    const PayrollCalculator::Result& payrollResult,
                    bool wagesVisible)
{
    const bool    rtl = isRtl();
    const QLocale loc = appLocale();
    const QString pa  = pAlign();   // primary alignment (reading edge)
    const QString sa  = sAlign();   // secondary (far edge)

    QString html;
    html.reserve(8192);

    // CSS: no direction/dir — only explicit text-align everywhere.
    // summary-label and summary-value alignments are injected via %1/%2.
    // Font stacks are injected via %3/%4 so CJK languages get correct fonts.
    html += QString(
    "<html><head><style>"
    "@page { margin: 18mm 20mm 18mm 20mm; }"
    "body  { font-family: %3; font-size: 10pt; color: #1a1a1a; text-align: center; }"
    ".report-header { border-top: 4px solid #2c3e50; padding-top: 10px; margin-bottom: 4px; }"
    ".report-title  { font-family: %4; font-size: 20pt;"
    "                 font-weight: bold; color: #2c3e50; margin:0; padding:0; }"
    ".report-sub    { font-size: 9pt; color: #555; margin-top: 4px; margin-bottom: 0; }"
    ".header-rule   { border:none; border-top:1px solid #aaa; margin: 10px 0 14px 0; }"
    ".section-title { font-family: %4; font-size: 11pt; font-weight: bold;"
    "                 color: #2c3e50; text-align: center; margin-top: 16px; margin-bottom: 6px;"
    "                 border-bottom: 1px solid #2c3e50; padding-bottom: 3px; }"
    ".summary-table { width:60%; margin:8px auto 0 auto; border-collapse:collapse; }"
    ".summary-table td { padding:5px 14px; font-size:10pt; border:none; }"
    ".summary-table tr:nth-child(odd) td { background-color:#f0f2f4; }"
    ".summary-label { text-align:%1; font-weight:bold; color:#333; width:55%; }"
    ".summary-value { text-align:%2; color:#1a1a1a; width:45%; }"
    ".summary-highlight td { font-size:11pt; font-weight:bold; color:#c0392b;"
    "                        background-color:#fdf0ee !important; }"
    ".attendance-table { width:100%; border-collapse:collapse; margin-top:6px; font-size:9pt; }"
    ".attendance-table th { background-color:#2c3e50; color:#fff; padding:6px 8px;"
    "                       text-align:center; font-size:9pt; font-weight:bold; }"
    ".attendance-table td { border:1px solid #ddd; padding:4px 8px; text-align:center; }"
    ".attendance-table tr.alt td { background-color:#f7f9fa; }"
    ".status-paid   { background-color:#e9f7ef; color:#1e8449; font-weight:bold; }"
    ".status-unpaid { background-color:#fdf0ee; color:#c0392b; font-weight:bold; }"
    ".footer { font-size:8pt; color:#777; margin-top:20px;"
    "          border-top:1px solid #ccc; padding-top:6px; text-align:center; }"
    "</style></head><body>").arg(pa).arg(sa).arg(sansStack()).arg(serifStack());

    // Header
    html += "<div class='report-header'>";
    html += "<p class='report-title'>Rawatib &mdash; " + tr("Monthly Salary Report") + "</p>";
    html += QString("<p class='report-sub'>"
                    "%1 &nbsp;&bull;&nbsp; %2 %3"
                    " &nbsp;&bull;&nbsp; %4: %5</p>")
                .arg(employeeName.toHtmlEscaped())
                .arg(loc.monthName(month, QLocale::LongFormat)).arg(year)
                .arg(tr("Printed"))
                .arg(QDate::currentDate().toString("yyyy-MM-dd"));
    if (s.isMonthly) {
        const DeductionPolicy::Mode dmode = DeductionPolicy::mode();
        QString modeStr = DeductionPolicy::modeLabel(dmode);
        if (dmode == DeductionPolicy::Mode::PerDay)
            modeStr += QString(" (%1%)").arg(DeductionPolicy::perDayPenaltyPct(), 0, 'f', 1);
        html += QString("<p class='report-sub'>%1: %2</p>")
                    .arg(tr("Deduction Mode"))
                    .arg(modeStr.toHtmlEscaped());
    }
    html += "</div><hr class='header-rule'>";

    // Summary
    html += "<p class='section-title'>" + tr("Summary") + "</p>";
    html += "<table class='summary-table'>";
    appendSummaryRows(html, s, false, s.unpaidAmount > 0);
    if (payrollResult.grossPay > 0)
        appendNetPayRows(html, payrollResult, false);
    html += "</table>";

    // Attendance table
    // RTL: columns are physically reversed so Status is leftmost, Date rightmost
    html += "<p class='section-title'>" + tr("Daily Attendance") + "</p>";
    if (records.isEmpty()) {
        html += "<p>" + tr("No attendance records for this period.") + "</p>";
    } else {
        html += "<table class='attendance-table'><thead><tr>";
        if (rtl) {
            html += "<th>" + tr("Status")    + "</th>";
            html += "<th>" + tr("Wage (%1)").arg(CurrencyManager::symbol()) + "</th>";
            html += "<th>" + tr("Hours")     + "</th>";
            html += "<th>" + tr("Check-Out") + "</th>";
            html += "<th>" + tr("Check-In")  + "</th>";
            html += "<th>" + tr("Date")      + "</th>";
        } else {
            html += "<th>" + tr("Date")      + "</th>";
            html += "<th>" + tr("Check-In")  + "</th>";
            html += "<th>" + tr("Check-Out") + "</th>";
            html += "<th>" + tr("Hours")     + "</th>";
            html += "<th>" + tr("Wage (%1)").arg(CurrencyManager::symbol()) + "</th>";
            html += "<th>" + tr("Status")    + "</th>";
        }
        html += "</tr></thead><tbody>";

        bool alt = false;
        for (const auto& r : records) {
            const QString rowClass  = alt ? " class='alt'" : "";
            const QString statusCls = r.paid ? "status-paid" : "status-unpaid";
            const QString statusTxt = r.paid ? tr("Paid") : tr("Unpaid");

            if (rtl) {
                html += QString("<tr%1>"
                                "<td class='%2'>%3</td>"
                                "<td>%4</td>"
                                "<td>%5</td>"
                                "<td>%6</td>"
                                "<td>%7</td>"
                                "<td>%8</td></tr>")
                            .arg(rowClass)
                            .arg(statusCls).arg(statusTxt)
                            .arg(CurrencyManager::formatHtml(r.dailyWage))
                            .arg(r.hoursWorked, 0,'f',2)
                            .arg(r.checkOut.toString("hh:mm AP"))
                            .arg(r.checkIn.toString("hh:mm AP"))
                            .arg(r.date.toString("yyyy-MM-dd"));
            } else {
                html += QString("<tr%1>"
                                "<td>%2</td>"
                                "<td>%3</td>"
                                "<td>%4</td>"
                                "<td>%5</td>"
                                "<td>%6</td>"
                                "<td class='%7'>%8</td></tr>")
                            .arg(rowClass)
                            .arg(r.date.toString("yyyy-MM-dd"))
                            .arg(r.checkIn.toString("hh:mm AP"))
                            .arg(r.checkOut.toString("hh:mm AP"))
                            .arg(r.hoursWorked, 0,'f',2)
                            .arg(CurrencyManager::formatHtml(r.dailyWage))
                            .arg(statusCls).arg(statusTxt);
            }
            alt = !alt;
        }
        html += "</tbody></table>";
    }

    // Footer
    html += QString("<p class='footer'>Rawatib &mdash; %1 &bull; %2 %3"
                    " &nbsp;|&nbsp; &copy; 2026 Rawatib</p>")
                .arg(employeeName.toHtmlEscaped())
                .arg(loc.monthName(month, QLocale::LongFormat)).arg(year);
    html += "</body></html>";
    return html;
}

// ── buildReceiptHtml ───────────────────────────────────────────────────────
// Thermal receipt: no columns to reverse — just text-align on summary lines.
// Receipt table has only 4 narrow columns; for RTL we reverse their order too.
QString buildReceiptHtml(const QString& employeeName, int month, int year,
                         const MonthlySummary& s,
                         const QVector<AttendanceRecord>& records,
                         const PayrollCalculator::Result& payrollResult,
                         int bodyPt, int smallPt,
                         bool wagesVisible)
{
    const bool    rtl = isRtl();
    const QLocale loc = appLocale();
    const QString pa  = pAlign();

    QString html;
    html.reserve(8192);

    html += QString(
    "<html><head><style>"
    "@page { margin: 3mm 2mm 6mm 2mm; }"
    "body  { font-family: %4; font-size: %1pt; color: #000; text-align: center; }"
    ".title   { font-size: %2pt; font-weight: bold; margin: 0 0 2px 0; padding: 0; }"
    ".sub     { font-size: %3pt; margin: 2px 0 4px 0; }"
    ".rule    { border:none; border-top:1px dashed #000; margin:5px 0; }"
    ".section { font-size: %2pt; font-weight: bold; margin: 6px 0 3px 0;"
    "           border-bottom: 1px solid #000; padding-bottom: 1px; }"
    ".summary-block   { font-size:%1pt; margin:0; padding:0; }"
    ".summary-block p { margin: 1px 0; }"
    "table { width:100%; border-collapse:collapse; font-size:%3pt; }"
    "th    { border-top:1px solid #000; border-bottom:1px solid #000;"
    "        padding:2px 1px; text-align:center; font-weight:bold; }"
    "td    { padding:2px 1px; text-align:center; border-bottom:1px dotted #ccc; }"
    ".unpaid { font-weight:bold; }"
    ".footer { font-size:%3pt; margin-top:8px; border-top:1px dashed #000;"
    "          padding-top:3px; text-align:center; }"
    "</style></head><body>")
    .arg(bodyPt).arg(bodyPt + 1).arg(smallPt).arg(sansStack());

    html += "<p class='title'>Rawatib</p>";
    html += "<p class='sub'>" + tr("Monthly Salary Report") + "</p>";
    html += QString("<p class='sub'><b>%1</b></p>").arg(employeeName.toHtmlEscaped());
    html += QString("<p class='sub'>%1 %2</p>")
                .arg(loc.monthName(month, QLocale::LongFormat)).arg(year);
    html += QString("<p class='sub'>%1</p>")
                .arg(QDate::currentDate().toString("yyyy-MM-dd"));
    if (s.isMonthly) {
        const DeductionPolicy::Mode dmode = DeductionPolicy::mode();
        QString modeStr = DeductionPolicy::modeLabel(dmode);
        if (dmode == DeductionPolicy::Mode::PerDay)
            modeStr += QString(" (%1%)").arg(DeductionPolicy::perDayPenaltyPct(), 0, 'f', 1);
        html += QString("<p class='sub'>%1: %2</p>")
                    .arg(tr("Deduction Mode"))
                    .arg(modeStr.toHtmlEscaped());
    }
    html += "<hr class='rule'>";

    html += "<p class='section'>" + tr("Summary") + "</p>";
    html += "<div class='summary-block'>";
    appendSummaryRows(html, s, true, s.unpaidAmount > 0);
    if (payrollResult.grossPay > 0)
        appendNetPayRows(html, payrollResult, true);
    html += "</div><hr class='rule'>";

    html += "<p class='section'>" + tr("Attendance") + "</p>";
    if (records.isEmpty()) {
        html += "<p>" + tr("No records.") + "</p>";
    } else {
        html += "<table><thead><tr>";
        if (rtl) {
            // RTL: Status | Wage | Hrs | Date
            html += "<th>" + tr("Status") + "</th>";
            html += "<th>" + tr("Wage")   + "</th>";
            html += "<th>" + tr("Hrs")    + "</th>";
            html += "<th>" + tr("Date")   + "</th>";
        } else {
            // LTR: Date | Hrs | Wage | Status
            html += "<th>" + tr("Date")   + "</th>";
            html += "<th>" + tr("Hrs")    + "</th>";
            html += "<th>" + tr("Wage")   + "</th>";
            html += "<th>" + tr("Status") + "</th>";
        }
        html += "</tr></thead><tbody>";

        for (const auto& r : records) {
            const QString stCell = r.paid
                ? QString("<td>%1</td>").arg(tr("P"))
                : QString("<td class='unpaid'>%1</td>").arg(tr("U"));
            const QString dateCell = QString("<td>%1</td>")
                                         .arg(r.date.toString("MM/dd"));
            const QString hrsCell  = QString("<td>%1</td>")
                                         .arg(r.hoursWorked, 0,'f',1);
            const QString wageCell = QString("<td>%1</td>")
                                         .arg(CurrencyManager::formatHtml(r.dailyWage));
            if (rtl)
                html += "<tr>" + stCell + wageCell + hrsCell + dateCell + "</tr>";
            else
                html += "<tr>" + dateCell + hrsCell + wageCell + stCell + "</tr>";
        }
        html += "</tbody></table>";
        html += QString("<p class='sub' style='text-align:%1;'>")
                    .arg(pa)
                + tr("P = Paid  /  U = Unpaid") + "</p>";
    }

    html += "<p class='footer'>&copy; 2026 Rawatib</p>";
    html += "</body></html>";
    return html;
}

// ── buildPaymentSlipHtml (A4) ──────────────────────────────────────────────
// Info table: RTL swaps label/value column order so label is on the right.
// Breakdown table: same swap.
// Signature table: stays centered — no swap needed.
QString buildPaymentSlipHtml(const QString& employeeName,
                              int month, int year,
                              double totalSalary,
                              double previouslyPaid,
                              double amountPaidNow)
{
    const bool    rtl = isRtl();
    const QLocale loc = appLocale();
    const QString pa  = pAlign();
    const QString sa  = sAlign();

    QString html;
    html.reserve(2048);

    html += QString(
    "<html><head><style>"
    "@page { margin: 18mm 20mm 18mm 20mm; }"
    "body  { font-family: %3; font-size: 11pt; color: #1a1a1a; text-align: center; }"
    ".slip-title  { font-family: %4; font-size: 22pt;"
    "               font-weight: bold; color: #2c3e50; margin: 0; padding: 0; }"
    ".slip-sub    { font-size: 9pt; color: #555; margin: 4px 0 0 0; }"
    ".rule-thick  { border:none; border-top: 3px solid #2c3e50; margin: 10px 0; }"
    ".rule-thin   { border:none; border-top: 1px solid #aaa; margin: 8px 0; }"
    ".info-table  { width: 60%; margin: 10px auto; border-collapse: collapse; }"
    ".info-table td { padding: 5px 14px; font-size: 11pt; border: none; }"
    ".info-label  { text-align: %1; font-weight: bold; color: #333; width: 50%; }"
    ".info-value  { text-align: %2; color: #1a1a1a; width: 50%; }"
    ".amount-box  { width: 60%; margin: 16px auto; border: 2px solid #2c3e50;"
    "               border-radius: 4px; padding: 10px; }"
    ".amount-label { font-size: 10pt; color: #555; margin: 0 0 4px 0; }"
    ".amount-value { font-family: %4; font-size: 22pt;"
    "                font-weight: bold; color: #2c3e50; margin: 0; }"
    ".breakdown   { width: 60%; margin: 8px auto; border-collapse: collapse;"
    "               font-size: 9pt; color: #555; }"
    ".breakdown td { padding: 2px 14px; border: none; }"
    ".bl { text-align: %1; } .bv { text-align: %2; }"
    ".sig-table   { width: 80%; margin: 30px auto 0 auto; border-collapse: collapse; }"
    ".sig-table td { padding: 4px 20px; text-align: center; font-size: 10pt; width: 50%; }"
    ".sig-line    { border-top: 1px solid #000; padding-top: 4px; font-size: 9pt; color: #555; }"
    ".footer      { font-size: 8pt; color: #777; margin-top: 20px;"
    "               border-top: 1px solid #ccc; padding-top: 6px; text-align: center; }"
    "</style></head><body>").arg(pa).arg(sa).arg(sansStack()).arg(serifStack());

    html += "<p class='slip-title'>Rawatib</p>";
    html += "<p class='slip-sub'>" + tr("Salary Payment Slip") + "</p>";
    html += "<hr class='rule-thick'>";

    // Info rows — RTL: value cell first (left), label cell second (right)
    auto infoRow = [&](const QString& label, const QString& value) {
        if (rtl)
            html += QString("<tr>"
                            "<td class='info-value'>%1</td>"
                            "<td class='info-label'>%2</td>"
                            "</tr>").arg(value).arg(label);
        else
            html += QString("<tr>"
                            "<td class='info-label'>%1</td>"
                            "<td class='info-value'>%2</td>"
                            "</tr>").arg(label).arg(value);
    };

    html += "<table class='info-table'>";
    infoRow(tr("Employee:"),     employeeName.toHtmlEscaped());
    infoRow(tr("Period:"),       QString("%1 %2")
                                     .arg(loc.monthName(month, QLocale::LongFormat))
                                     .arg(year));
    infoRow(tr("Date of Payment:"), QDate::currentDate().toString("yyyy-MM-dd"));
    html += "</table><hr class='rule-thin'>";

    // Amount box — centered, no swap needed
    html += "<div class='amount-box'>";
    html += "<p class='amount-label'>" + tr("Amount Paid") + "</p>";
    html += "<p class='amount-value'>" + CurrencyManager::formatHtml(amountPaidNow) + "</p>";
    html += "</div>";

    // Breakdown — RTL: value left, label right
    if (previouslyPaid > 0.0) {
        auto bRow = [&](const QString& label, const QString& value, bool bold = false) {
            const QString l = bold ? "<b>" + label + "</b>" : label;
            const QString v = bold ? "<b>" + value + "</b>" : value;
            if (rtl)
                html += QString("<tr><td class='bv'>%1</td><td class='bl'>%2</td></tr>")
                            .arg(v).arg(l);
            else
                html += QString("<tr><td class='bl'>%1</td><td class='bv'>%2</td></tr>")
                            .arg(l).arg(v);
        };
        html += "<table class='breakdown'>";
        bRow(tr("Total Month Salary:"), CurrencyManager::formatHtml(totalSalary));
        bRow(tr("Previously Paid:"),    CurrencyManager::formatHtml(previouslyPaid));
        bRow(tr("Paid This Time:"),     CurrencyManager::formatHtml(amountPaidNow), true);
        html += "</table>";
    }

    html += "<hr class='rule-thin'>";

    // Signature lines — centered, always same order
    html += "<table class='sig-table'><tr>";
    html += "<td><div class='sig-line'>" + tr("Employer Signature") + "</div></td>";
    html += "<td><div class='sig-line'>" + tr("Employee Signature") + "</div></td>";
    html += "</tr></table>";

    html += QString("<p class='footer'>Rawatib &mdash; %1 &bull; %2 %3"
                    " &nbsp;|&nbsp; &copy; 2026 Rawatib</p>")
                .arg(employeeName.toHtmlEscaped())
                .arg(loc.monthName(month, QLocale::LongFormat)).arg(year);
    html += "</body></html>";
    return html;
}

// ── buildPaymentSlipReceiptHtml (thermal) ──────────────────────────────────
// Thermal slip: single-column layout — just text-align, no column swapping.
QString buildPaymentSlipReceiptHtml(const QString& employeeName,
                                     int month, int year,
                                     double totalSalary,
                                     double previouslyPaid,
                                     double amountPaidNow,
                                     int bodyPt, int smallPt)
{
    const QLocale loc = appLocale();
    const QString pa  = pAlign();

    QString html;
    html.reserve(1024);

    html += QString(
    "<html><head><style>"
    "@page { margin: 3mm 2mm 8mm 2mm; }"
    "body  { font-family: %5; font-size: %1pt; color: #000; text-align: center; }"
    ".title  { font-size: %2pt; font-weight: bold; margin: 0; padding: 0; }"
    ".sub    { font-size: %3pt; margin: 2px 0; }"
    ".rule   { border:none; border-top:1px dashed #000; margin:4px 0; }"
    ".amount { font-size: %2pt; font-weight: bold; margin: 4px 0; }"
    ".info   { font-size: %1pt; margin: 1px 0; text-align: %4; }"
    ".sig    { margin-top: 10px; font-size: %3pt; }"
    ".sig-line { border-top: 1px solid #000; margin-top: 14px; padding-top: 2px; }"
    ".footer { font-size: %3pt; margin-top: 6px;"
    "          border-top: 1px dashed #000; padding-top: 3px; }"
    "</style></head><body>")
    .arg(bodyPt).arg(bodyPt + 1).arg(smallPt).arg(pa).arg(sansStack());

    html += "<p class='title'>Rawatib</p>";
    html += "<p class='sub'>" + tr("Salary Payment Slip") + "</p>";
    html += "<hr class='rule'>";

    html += QString("<p class='info'><b>%1</b> %2</p>")
                .arg(tr("Employee:")).arg(employeeName.toHtmlEscaped());
    html += QString("<p class='info'><b>%1</b> %2 %3</p>")
                .arg(tr("Period:"))
                .arg(loc.monthName(month, QLocale::LongFormat)).arg(year);
    html += QString("<p class='info'><b>%1</b> %2</p>")
                .arg(tr("Date:"))
                .arg(QDate::currentDate().toString("yyyy-MM-dd"));

    html += "<hr class='rule'>";
    html += "<p class='sub'>" + tr("Amount Paid") + "</p>";
    html += "<p class='amount'>" + CurrencyManager::formatHtml(amountPaidNow) + "</p>";

    if (previouslyPaid > 0.0) {
        html += "<hr class='rule'>";
        html += QString("<p class='info'>%1 %2</p>")
                    .arg(tr("Total Salary:")).arg(CurrencyManager::formatHtml(totalSalary));
        html += QString("<p class='info'>%1 %2</p>")
                    .arg(tr("Prev. Paid:")).arg(CurrencyManager::formatHtml(previouslyPaid));
        html += QString("<p class='info'><b>%1 %2</b></p>")
                    .arg(tr("This Payment:")).arg(CurrencyManager::formatHtml(amountPaidNow));
    }

    html += "<hr class='rule'>";
    html += "<div class='sig'>";
    html += "<div class='sig-line'>" + tr("Employer Signature") + "</div>";
    html += "<div class='sig-line'>" + tr("Employee Signature") + "</div>";
    html += "</div>";

    html += "<p class='footer'>&copy; 2026 Rawatib</p>";
    html += "</body></html>";
    return html;
}

// ── printMonthlyReport ─────────────────────────────────────────────────────
void printMonthlyReport(const QString& employeeName,
                        int employeeId,
                        int month, int year,
                        const PayrollCalculator::Result& payrollResult,
                        QWidget* parent,
                        bool wagesVisible)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);

    QPrintDialog dlg(&printer, parent);
    dlg.setWindowTitle(tr("Print Monthly Report"));
    if (dlg.exec() != QDialog::Accepted) return;

    auto s = AttendanceRepository::instance()
                 .getMonthlySummary(employeeId, year, month);
    auto records = AttendanceRepository::instance()
                       .getRecordsForMonth(employeeId, year, month);

    const double widthMM = printer.pageRect(QPrinter::Millimeter).width();
    QString html;
    if (widthMM >= 100.0)
        html = buildA4Html(employeeName, month, year, s, records, payrollResult, wagesVisible);
    else if (widthMM >= 65.0)
        html = buildReceiptHtml(employeeName, month, year, s, records, payrollResult, 8, 7, wagesVisible);
    else
        html = buildReceiptHtml(employeeName, month, year, s, records, payrollResult, 7, 6, wagesVisible);

    QTextDocument doc;
    doc.setPageSize(printer.pageRect(QPrinter::Point).size());
    doc.setHtml(html);
    doc.print(&printer);
}

// ── printPaymentSlip ───────────────────────────────────────────────────────
void printPaymentSlip(const QString& employeeName,
                      int month, int year,
                      double totalSalary,
                      double previouslyPaid,
                      double amountPaidNow,
                      QWidget* parent)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);

    QPrintDialog dlg(&printer, parent);
    dlg.setWindowTitle(tr("Print Payment Slip"));
    if (dlg.exec() != QDialog::Accepted) return;

    const double widthMM = printer.pageRect(QPrinter::Millimeter).width();
    QString html;
    if (widthMM >= 100.0)
        html = buildPaymentSlipHtml(employeeName, month, year,
                                    totalSalary, previouslyPaid, amountPaidNow);
    else if (widthMM >= 65.0)
        html = buildPaymentSlipReceiptHtml(employeeName, month, year,
                                           totalSalary, previouslyPaid,
                                           amountPaidNow, 8, 7);
    else
        html = buildPaymentSlipReceiptHtml(employeeName, month, year,
                                           totalSalary, previouslyPaid,
                                           amountPaidNow, 7, 6);

    QTextDocument doc;
    doc.setPageSize(printer.pageRect(QPrinter::Point).size());
    doc.setHtml(html);
    doc.print(&printer);
}

} // namespace PrintHelper
