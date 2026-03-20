# Changelog

All notable changes to Rawatib will be documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] — 2026-03-20

### Employee Management
- Add, edit, and delete employees with name, phone, and notes
- Two pay types: hourly (hours × rate) and monthly salary with proportional deductions
- Set expected check-in / check-out times and late tolerance per employee
- Per-employee 6–12 digit PIN for kiosk self-service

### Attendance Tracking
- Record check-in and check-out times with per-session wage calculation
- Multiple sessions per day supported with automatic overlap detection
- Kiosk mode: employees check themselves in and out using their PIN
- Edit any record at any time — wages recalculate automatically on save

### Payroll & Salary
- Three configurable deduction modes for monthly employees: per-minute, per-day penalty, and off
- Global payroll rules (deductions and additions) with fixed or percentage-of-gross basis
- Monthly salary summary with full deduction breakdown and net pay calculation
- One-click Pay Entire Month — marks all records as paid and prints a payment slip

### Export, Import & Print
- Export monthly reports to XLSX (with formulas) or CSV
- Two-pass CSV import with full conflict preview before committing
- Formatted print reports for A4 and receipt paper sizes
- Receipt status abbreviations are language-native: 済/未 (Japanese), 已/未 (Chinese), 지/미 (Korean), م/غ (Arabic), P/N (French), Ö/ÖD (Turkish)

### Security
- AES-256 database encryption via SQLCipher, always on
- Admin PIN with PBKDF2-SHA256 hashing (100,000 iterations) and OS keychain integration
- Configurable lock policy — choose which actions require admin unlock
- Brute-force protection: progressive lockout after 5 failed attempts
- Tamper-evident SHA-256 hash-chain audit log with integrity verification
- Emergency recovery file system for locked-out scenarios
- Network request URL validation on the update checker

### Update Checker
- Automatic silent background check on launch — badges Help menu if a new version is found
- Manual check via Help → Check for Updates with release notes in expandable details
- Dismissed versions are remembered — no repeated notifications for the same version

### UI & Experience
- Full Arabic RTL interface with right-to-left layout
- French, Turkish, Korean, Japanese, and Chinese translations included
- Dark mode support — custom colors adapt to the system theme with live updates on mid-session theme change
- Locale-aware employee list sorting — correct alphabetical order for Arabic and all supported languages
- Window launches at a comfortable fixed size that works well across all common screen sizes
- First-run setup dialog for language and currency selection
- 27 built-in tips accessible from Help → Discover Rawatib

### Platform & Infrastructure
- Cross-platform: Windows, Linux (AppImage + .deb), macOS
- Auto-backup on every launch with configurable interval
- Single-instance lock — prevents opening the app twice against the same database
- Command-line options: `--lang`, `--db-path`, `--reset`, `--reset-all`, `--bypass-key`, `--dev-mode`
