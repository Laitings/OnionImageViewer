#include "SingleInstance.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLocalSocket>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace SingleInstance {

QString serverName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    return QStringLiteral("OnionImageViewer-%1").arg(user);
}

bool forwardToRunning(const QString &path)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(400)) {
        return false;
    }
#ifdef Q_OS_WIN
    // This process was just started by Explorer and may bring a window to the front; pass that
    // right on to the running instance.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    socket.write(QFileInfo(path).absoluteFilePath().toUtf8() + '\n');
    const bool sent = socket.waitForBytesWritten(2000);
    socket.disconnectFromServer();
    return sent;
}

} // namespace SingleInstance
