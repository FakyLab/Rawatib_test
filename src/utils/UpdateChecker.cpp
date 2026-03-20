#include "utils/UpdateChecker.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>
#include <QCoreApplication>
#include <QUrl>

// Translation shorthand — context matches <context><n> in .ts files
static inline QString tr(const char* key) {
    return QCoreApplication::translate("UpdateChecker", key);
}

// ── GitHub Releases API endpoint ──────────────────────────────────────────
// Uses FakyLab — the public GitHub organisation name.
static constexpr const char* API_URL =
    "https://api.github.com/repos/FakyLab/Rawatib/releases/latest";

// Fallback URL opened if the release URL from GitHub fails validation
static constexpr const char* FALLBACK_URL =
    "https://github.com/FakyLab/Rawatib/releases";

// Network timeout in milliseconds — avoids hanging on slow/no connection
static constexpr int TIMEOUT_MS = 10000;

// ── URL validation ────────────────────────────────────────────────────────
// Validates that a URL received from the GitHub API response is a safe
// HTTPS github.com URL before it is stored or opened in the browser.
// Rejects anything that is not HTTPS or not on github.com — even if the
// TLS connection to GitHub was valid, we do not want to open arbitrary URLs
// that could appear in a tampered or unexpected API response.
static bool isSafeGitHubUrl(const QString& urlStr) {
    const QUrl url(urlStr);
    if (!url.isValid())                          return false;
    if (url.scheme() != QLatin1String("https"))  return false;
    const QString host = url.host().toLower();
    if (host != QLatin1String("github.com")
        && !host.endsWith(QLatin1String(".github.com"))) return false;
    return true;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

void UpdateChecker::check(bool silent) {
    m_silent = silent;

    QNetworkRequest req{QUrl(QString::fromLatin1(API_URL))};

    // GitHub API requires a User-Agent header — identify as Rawatib + version
    req.setRawHeader("User-Agent",
        QByteArray("Rawatib/") + QCoreApplication::applicationVersion().toUtf8());

    // Request JSON response
    req.setRawHeader("Accept", "application/vnd.github+json");

    // Abort if the server doesn't respond within TIMEOUT_MS
    req.setTransferTimeout(TIMEOUT_MS);

    // Explicit redirect policy — allows HTTPS→HTTPS redirects,
    // blocks any downgrade to HTTP. Stated explicitly rather than
    // relying on Qt's default so the intent is clear and immune to
    // default policy changes across Qt versions.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        // ── Network error ─────────────────────────────────────────────────
        if (reply->error() != QNetworkReply::NoError) {
            // Silent mode: swallow network errors — user is not expecting a result
            if (!m_silent)
                emit checkFailed(reply->errorString());
            deleteLater();
            return;
        }

        // ── Parse JSON ────────────────────────────────────────────────────
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);

        if (doc.isNull() || !doc.isObject()) {
            if (!m_silent)
                emit checkFailed(tr("Received an unexpected response from the server."));
            deleteLater();
            return;
        }

        const QJsonObject obj = doc.object();

        // GitHub returns { "message": "Not Found" } for repos with no releases
        if (obj.contains(QStringLiteral("message"))) {
            if (!m_silent)
                emit checkFailed(tr("No releases found."));
            deleteLater();
            return;
        }

        // ── Extract fields ────────────────────────────────────────────────
        QString tag   = obj.value(QStringLiteral("tag_name")).toString().trimmed();
        QString url   = obj.value(QStringLiteral("html_url")).toString().trimmed();
        QString notes = obj.value(QStringLiteral("body")).toString().trimmed();

        // Tag format is "v1.2.0" — strip the leading 'v' for QVersionNumber
        if (tag.startsWith(QLatin1Char('v')))
            tag.remove(0, 1);

        if (tag.isEmpty() || url.isEmpty()) {
            if (!m_silent)
                emit checkFailed(tr("Could not read version information from the server."));
            deleteLater();
            return;
        }

        // ── Validate the release URL before passing it out ────────────────
        // Must be a valid HTTPS github.com URL. If not, fall back to the
        // known-good releases page so the user still lands somewhere useful.
        if (!isSafeGitHubUrl(url))
            url = QString::fromLatin1(FALLBACK_URL);

        // ── Version comparison ────────────────────────────────────────────
        const QVersionNumber latest  = QVersionNumber::fromString(tag);
        const QVersionNumber current = QVersionNumber::fromString(
            QCoreApplication::applicationVersion());

        if (latest > current) {
            emit updateAvailable(tag, url, notes);
        } else {
            // Only report "up to date" on manual checks
            if (!m_silent)
                emit alreadyUpToDate();
        }

        deleteLater();
    });
}
