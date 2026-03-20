#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// ── UpdateChecker ─────────────────────────────────────────────────────────
//
// Queries the GitHub Releases API for the latest Rawatib release and
// compares it against the running version.
//
// Usage — manual check (always reports result):
//   auto* c = new UpdateChecker(this);
//   connect(c, &UpdateChecker::updateAvailable, ...);
//   connect(c, &UpdateChecker::alreadyUpToDate, ...);
//   connect(c, &UpdateChecker::checkFailed,     ...);
//   c->check(false);
//
// Usage — silent background check (only emits updateAvailable):
//   auto* c = new UpdateChecker(this);
//   connect(c, &UpdateChecker::updateAvailable, ...);
//   c->check(true);
//
// The object deletes itself after emitting — no manual cleanup needed.
// Network timeout: 10 seconds. No retries on failure.
// No personal data is transmitted — only the app version in User-Agent.

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // silent = true  → only emits updateAvailable (background use)
    // silent = false → emits all three signals (manual check from menu)
    void check(bool silent);

signals:
    void updateAvailable(const QString& latestVersion,
                         const QString& releaseUrl,
                         const QString& releaseNotes);
    void alreadyUpToDate();
    void checkFailed(const QString& errorMessage);

private:
    QNetworkAccessManager* m_nam  = nullptr;
    bool                   m_silent = false;
};
