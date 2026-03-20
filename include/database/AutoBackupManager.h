#pragma once
#include <QString>

class AutoBackupManager {
public:
    static AutoBackupManager& instance();

    // Call once on startup after DB is initialized.
    // Silently creates a backup and prunes old ones.
    // Returns true if backup was created successfully.
    bool runStartupBackup(const QString& dbPath, int maxBackupsToKeep = 5);

    // Returns the folder where auto-backups are stored.
    static QString backupFolder();

    // Returns the last message (success or reason for skip/failure).
    QString lastMessage() const;

private:
    AutoBackupManager() = default;
    bool pruneOldBackups(int maxToKeep);

    QString m_dbPath;
    QString m_lastMessage;
};
