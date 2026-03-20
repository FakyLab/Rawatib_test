#include <QApplication>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QIcon>
#include <QDebug>
#include <QLockFile>
#include <QTranslator>
#include <QLibraryInfo>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QtPlugin>

// Import the statically linked SQLCipher driver so Qt's SQL system
// can find "QSQLCIPHER" without a plugin DLL in the file system.
Q_IMPORT_PLUGIN(QSQLCipherDriverPlugin)

#include "database/DatabaseManager.h"
#include "database/AutoBackupManager.h"
#include "ui/MainWindow.h"
#include "ui/dialogs/DatabaseUnlockDialog.h"
#include "ui/dialogs/FirstRunDialog.h"
#include "utils/SettingsManager.h"
#include "utils/PinManager.h"
#include "utils/LanguageRegistry.h"


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Rawatib");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("FakyLab");

    app.setWindowIcon(QIcon(":/resources/app_icon.png"));

    // ── Parse command-line arguments ───────────────────────────────────────
    QCommandLineParser parser;
    parser.setApplicationDescription("Rawatib - Employee Attendance & Payroll Manager");

    QCommandLineOption devModeOption("dev-mode", "Enable developer tools menu.");
    QCommandLineOption langOption("lang",
        "Override UI language for this session only (en/ar). Does not save.",
        "code");
    QCommandLineOption dbPathOption("db-path",
        "Override database path for this session only. Does not affect saved settings.",
        "path");
    QCommandLineOption bypassKeyOption("bypass-key",
        "Emergency PIN recovery: requires --recovery-file. Removes the admin PIN and opens the app unlocked.");
    QCommandLineOption recoveryFileOption("recovery-file",
        "Path to the recovery file generated when the PIN was set (required with --bypass-key).",
        "path");
    QCommandLineOption resetOption("reset",
        "Clear all saved settings (language, session timeout, payroll toggle) and restart. "
        "Database is not affected.");
    QCommandLineOption resetAllOption("reset-all",
        "Delete the database and clear all settings, then restart. "
        "Use as a last resort when locked out with no recovery file. "
        "All data will be permanently lost.");

    parser.addOption(devModeOption);
    parser.addOption(langOption);
    parser.addOption(dbPathOption);
    parser.addOption(bypassKeyOption);
    parser.addOption(recoveryFileOption);
    parser.addOption(resetOption);
    parser.addOption(resetAllOption);
    parser.parse(app.arguments());

    const bool devMode         = parser.isSet(devModeOption);
    bool bypassKey                   = parser.isSet(bypassKeyOption);
    const QString recoveryFilePath = parser.isSet(recoveryFileOption)
                                         ? parser.value(recoveryFileOption)
                                         : QString();
    const QString langOverride = parser.isSet(langOption)
                                     ? parser.value(langOption).toLower()
                                     : QString();
    const QString dbPathOverride = parser.isSet(dbPathOption)
                                       ? parser.value(dbPathOption)
                                       : QString();

    // ── First-run setup (language + currency) ────────────────────────────
    // Must run BEFORE translator installation so that the chosen language
    // takes effect from the very first MainWindow render — no restart needed.
    // On subsequent launches, isFirstRun() returns false and this is a no-op.
    if (!langOverride.isEmpty()) {
        // Dev --lang override: skip first-run dialog, force the override.
        // Only apply if the code is a known registered language.
        if (LanguageRegistry::find(langOverride).code == langOverride)
            SettingsManager::setLanguage(langOverride);
    } else {
        FirstRunDialog::showIfNeeded(nullptr);
    }

    // ── Language & layout direction ────────────────────────────────────────
    // Read language now — first-run dialog may have just saved a new value.
    const QString savedLang = SettingsManager::getLanguage();
    const QString lang = (!langOverride.isEmpty() &&
                          LanguageRegistry::find(langOverride).code == langOverride)
                             ? langOverride : savedLang;

    // Install Qt's own translation for standard dialog buttons (OK, Cancel, etc.)
    // Priority: embedded resource → Qt installation dir → translations/ next to exe
    QTranslator qtTranslator;
    if (lang != "en") {
        // Map app language codes to Qt's actual qtbase filename suffixes.
        // Qt ships qtbase_zh_CN.qm, not qtbase_zh.qm.
        // Extend this map if other languages have non-matching qtbase names.
        auto qtBaseLang = [](const QString& code) -> QString {
            if (code == "zh") return QStringLiteral("zh_CN");
            return code;
        };
        const QString qtLang    = qtBaseLang(lang);
        const QString qtTrName  = QString("qtbase_%1").arg(qtLang);
        const QString resPath   = QString(":/i18n/qtbase_%1.qm").arg(qtLang);
        const QString qtTrDir   = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        const QString appTrDir  = QCoreApplication::applicationDirPath() + "/translations";

        if (!qtTranslator.load(resPath)) {
            if (!qtTranslator.load(qtTrName, qtTrDir)) {
                const bool loaded = qtTranslator.load(qtTrName, appTrDir);
                Q_UNUSED(loaded)
            }
        }
        if (!qtTranslator.isEmpty())
            app.installTranslator(&qtTranslator);
    }

    // Install the app's own translation for the chosen language.
    // English is the source language — no .qm file needed for it.
    QTranslator translator;
    if (lang != "en") {
        const QString resPath  = QString(":/i18n/rawatib_%1.qm").arg(lang);
        const QString appTrDir = QCoreApplication::applicationDirPath() + "/translations";
        if (!translator.load(resPath)) {
            const bool loaded = translator.load(QString("rawatib_%1").arg(lang), appTrDir);
            Q_UNUSED(loaded)
        }
        if (!translator.isEmpty())
            app.installTranslator(&translator);
    }

    // Set layout direction — driven by LanguageRegistry, not a hardcoded language check.
    app.setLayoutDirection(LanguageRegistry::isRtl(lang)
                           ? Qt::RightToLeft
                           : Qt::LeftToRight);

    // ── Database setup ─────────────────────────────────────────────────────
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    const QString dbPath = !dbPathOverride.isEmpty()
                               ? dbPathOverride
                               : dataDir + "/attendance.db";

    if (!dbPathOverride.isEmpty())
        QDir().mkpath(QFileInfo(dbPath).absolutePath());

    // ── Single instance lock ───────────────────────────────────────────────
    const QString lockPath = dbPath + ".lock";
    QLockFile lockFile(lockPath);
    if (!lockFile.tryLock(100)) {
        QMessageBox::information(nullptr, "Rawatib",
            QCoreApplication::translate("main",
                "Rawatib is already running.\n\n"
                "Check the taskbar or system tray."));
        return 0;
    }

    qDebug() << "Database path:" << dbPath;
    if (devMode)                   qDebug() << "[dev-mode] active";
    if (!dbPathOverride.isEmpty()) qDebug() << "[dev-mode] db-path override:" << dbPath;
    if (!langOverride.isEmpty())   qDebug() << "[dev-mode] lang override:" << lang;

    // ── --reset: clear QSettings and restart ─────────────────────────────
    // No confirmation needed — only clears preferences, data is untouched.
    if (parser.isSet(resetOption)) {
        qDebug() << "[--reset] Clearing settings and restarting.";
        SettingsManager::resetAll();
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
        return 0;
    }

    // ── --reset-all: delete DB + clear settings and restart ───────────────
    // Last resort for users locked out with no recovery file.
    // One confirmation dialog — typing --reset-all already signals intent.
    if (parser.isSet(resetAllOption)) {
        qDebug() << "[--reset-all] Destructive reset requested.";
        const auto confirm = QMessageBox::warning(nullptr,
            QCoreApplication::translate("main", "Delete All Data — Are You Sure?"),
            QCoreApplication::translate("main",
                "This will permanently delete your database and all saved settings.\n\n"
                "All employees, attendance records, payroll rules, and audit log "
                "will be lost forever.\n\n"
                "Make sure you have exported your data before continuing.\n\n"
                "This cannot be undone. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (confirm == QMessageBox::Yes) {
            qDebug() << "[--reset-all] Confirmed. Deleting DB and settings.";
            PinManager::deleteKey();
            QFile::remove(dbPath);
            QFile::remove(lockPath);
            SettingsManager::resetAll();
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
        } else {
            qDebug() << "[--reset-all] Cancelled.";
        }
        return 0;
    }

    // ── --bypass-key: emergency PIN recovery ─────────────────────────────
    // Requires --recovery-file <path> pointing to the file generated when
    // the PIN was set. Silent failure on missing/wrong file — no hint given.
    //
    // Sequence:
    //   1. Validate recovery file exists and matches hash in DB.
    //   2. Capture the user key from keychain BEFORE opening the DB.
    //   3. initialize() opens the DB using the captured key.
    //   4. DB is open — verify recovery file against stored hash.
    //   5. forceRemovePin(), rotateInstallSecret(), setBypassTimestamp().
    //   6. decryptDatabase() rekeys from user key → fallbackKey().

    // Step 1: pre-DB validation — file must exist
    if (bypassKey && (recoveryFilePath.isEmpty() ||
                      !QFileInfo::exists(recoveryFilePath))) {
        qDebug() << "[bypass-key] No valid recovery file path — ignoring flag.";
        bypassKey = false;
    }

    QByteArray bypassCipherKey;
    if (bypassKey) {
        bypassCipherKey = PinManager::loadKey();
        if (!bypassCipherKey.isEmpty())
            qDebug() << "[bypass-key] PIN-derived cipher key captured from keychain.";
        else
            qDebug() << "[bypass-key] No PIN cipher key found — nothing to bypass.";
    }

    auto& dbManager = DatabaseManager::instance();

    if (!dbManager.initialize(dbPath, bypassCipherKey)) {
        // Check if this is a key mismatch on an existing DB (wrong keychain entry,
        // OS reinstall, or machine migration) rather than a genuine DB error.
        const QFileInfo dbFileInfo(dbPath);
        const bool existingDb = dbFileInfo.exists() && dbFileInfo.size() > 4096;

        if (existingDb) {
            // Existing DB with wrong key — ask the admin for their PIN
            qDebug() << "[startup] DB key mismatch on existing DB — showing unlock dialog.";
            if (!DatabaseUnlockDialog::unlock(dbPath)) {
                // Admin cancelled — exit gracefully
                return 0;
            }
            // DB is now open and keychain has been updated — continue normally
        } else {
            // Genuine initialization error (corrupt file, permissions, etc.)
            QMessageBox::critical(nullptr,
                QCoreApplication::translate("main", "Database Error"),
                QCoreApplication::translate("main",
                    "Failed to initialize database:\n%1\n\nPath: %2")
                    .arg(dbManager.lastError(), dbPath));
            return 1;
        }
    }

    // ── Complete --bypass-key sequence now that DB is open ────────────────
    if (bypassKey && !bypassCipherKey.isEmpty()) {
        // Verify recovery file against the hash stored in the encrypted DB
        if (!PinManager::verifyRecoveryFile(recoveryFilePath)) {
            qDebug() << "[bypass-key] Recovery file verification FAILED — aborting bypass.";
            // Continue as normal launch — PIN prompt will appear
        } else {
            qDebug() << "[bypass-key] Recovery file verified OK.";

            PinManager::forceRemovePin();
            PinManager::rotateInstallSecret();
            PinManager::setBypassTimestamp();
            PinManager::setBypassPinWasSet(true);
            qDebug() << "[bypass-key] PIN removed. install_secret rotated.";

            if (!dbManager.decryptDatabase(bypassCipherKey)) {
                QMessageBox::warning(nullptr,
                    QCoreApplication::translate("main", "Rekey Warning"),
                    QCoreApplication::translate("main",
                        "Password was removed but the database could not be rekeyed "
                        "to the default key:\n%1\n\n"
                        "The app will continue this session, but please set a "
                        "new password immediately to avoid issues on next launch.")
                    .arg(dbManager.lastError()));
            } else {
                qDebug() << "[bypass-key] Database rekeyed to fallback key.";
            }
        }
    }

    AutoBackupManager::instance().runStartupBackup(dbPath, 5);

    MainWindow window(devMode, dbPath);


    window.show();

    return app.exec();
}
