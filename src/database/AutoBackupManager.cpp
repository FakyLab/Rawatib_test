#include "database/AutoBackupManager.h"
#include "database/DatabaseManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDateTime>
#include <QStandardPaths>
#include <QDebug>

AutoBackupManager& AutoBackupManager::instance() {
    static AutoBackupManager inst;
    return inst;
}

QString AutoBackupManager::backupFolder() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/auto_backups";
}

QString AutoBackupManager::lastMessage() const {
    return m_lastMessage;
}

bool AutoBackupManager::runStartupBackup(const QString& dbPath, int maxBackupsToKeep) {
    m_dbPath = dbPath;

    // Don't back up if the DB file doesn't exist yet (first ever launch)
    if (!QFile::exists(dbPath)) {
        m_lastMessage = "Skipped: database does not exist yet.";
        qDebug() << "[AutoBackup]" << m_lastMessage;
        return false;
    }

    // Don't back up an empty/brand-new database (smaller than 8 KB)
    const qint64 dbSize = QFileInfo(dbPath).size();
    if (dbSize < 8192) {
        m_lastMessage = "Skipped: database is empty (new installation).";
        qDebug() << "[AutoBackup]" << m_lastMessage;
        return false;
    }

    // Ensure backup folder exists
    QDir dir;
    if (!dir.mkpath(backupFolder())) {
        m_lastMessage = "Failed: could not create backup folder.";
        qWarning() << "[AutoBackup]" << m_lastMessage;
        return false;
    }

    // Build timestamped filename: auto_2025-03-07_143022.db
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    const QString destPath  = backupFolder() + "/auto_" + timestamp + ".db";

    // Close DB, copy file, reopen with key re-applied.
    // SQLCipher requires PRAGMA key as the first statement after every open().
    // Skipping reapplyKey() here leaves the connection unkeyed after backup,
    // breaking all subsequent queries on an encrypted database.
    auto& dbManager = DatabaseManager::instance();
    dbManager.database().close();
    const bool ok = QFile::copy(dbPath, destPath);
    dbManager.database().open();
    dbManager.reapplyKey();

    if (!ok) {
        m_lastMessage = QString("Failed: could not write to %1").arg(destPath);
        qWarning() << "[AutoBackup]" << m_lastMessage;
        return false;
    }

    m_lastMessage = QString("Backup created: %1").arg(destPath);
    qDebug() << "[AutoBackup]" << m_lastMessage;

    // Prune old backups, keeping only the most recent N
    pruneOldBackups(maxBackupsToKeep);

    return true;
}

bool AutoBackupManager::pruneOldBackups(int maxToKeep) {
    QDir dir(backupFolder());
    dir.setNameFilters({"auto_*.db"});
    dir.setSorting(QDir::Name | QDir::Reversed); // newest first (timestamp in name)

    QFileInfoList files = dir.entryInfoList(QDir::Files);

    if (files.size() <= maxToKeep) return true;

    // Delete everything beyond the newest N
    for (int i = maxToKeep; i < files.size(); ++i) {
        const QString path = files[i].absoluteFilePath();
        if (QFile::remove(path))
            qDebug() << "[AutoBackup] Pruned old backup:" << path;
        else
            qWarning() << "[AutoBackup] Could not remove old backup:" << path;
    }

    return true;
}
