#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class UpdateChecker final : public QObject
{
    Q_OBJECT

public:
    enum class Status { Available, UpToDate, NoStableRelease, NetworkError, InvalidVersion };
    Q_ENUM(Status)

    explicit UpdateChecker(QObject *parent = nullptr);

    // Reuses an in-progress request so startup and About can share the result.
    void check();
    bool isChecking() const;

signals:
    void finished(UpdateChecker::Status status, const QString &version, const QUrl &releaseUrl);

private:
    QNetworkAccessManager m_network;
    bool m_checking = false;
};
