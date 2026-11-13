#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QVersionNumber>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

bool UpdateChecker::isChecking() const
{
    return m_checking;
}

void UpdateChecker::check()
{
    if (m_checking) {
        return;
    }
    m_checking = true;

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/Laitings/OnionImageViewer/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "OnionImageViewer");
    request.setTransferTimeout(10000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_checking = false;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 404) {
            emit finished(Status::NoStableRelease, {}, {});
            return;
        }
        if (reply->error() != QNetworkReply::NoError || code != 200) {
            emit finished(Status::NetworkError, {}, {});
            return;
        }

        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        // Stable releases have numeric versions. Build metadata does not affect ordering.
        static const QRegularExpression versionPattern(QStringLiteral(
            R"(^[vV]?([0-9]+(?:\.[0-9]+){0,3})(-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$)"));
        const auto stable = versionPattern.match(tag);
        const auto installed = versionPattern.match(QCoreApplication::applicationVersion());
        const QString latestNumber = stable.captured(1);
        const QString currentNumber = installed.captured(1);
        qsizetype latestEnd = 0;
        qsizetype currentEnd = 0;
        const auto latest = QVersionNumber::fromString(latestNumber, &latestEnd).normalized();
        const auto current = QVersionNumber::fromString(currentNumber, &currentEnd).normalized();
        if (latestNumber.isEmpty() || currentNumber.isEmpty() || !stable.captured(2).isEmpty()
            || latestEnd != latestNumber.size() || currentEnd != currentNumber.size()) {
            emit finished(Status::InvalidVersion, {}, {});
            return;
        }
        const int order = QVersionNumber::compare(latest, current);
        if (order > 0 || (order == 0 && !installed.captured(2).isEmpty())) {
            const QByteArray url = "https://github.com/Laitings/OnionImageViewer/releases/tag/" + QUrl::toPercentEncoding(tag);
            emit finished(Status::Available, tag, QUrl::fromEncoded(url));
        } else {
            emit finished(Status::UpToDate, {}, {});
        }
    });
}
