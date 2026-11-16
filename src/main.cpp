#include "MainWindow.h"
#include "Preferences.h"
#include "SingleInstance.h"
#include "Version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QSettings>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("OnionImageViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("OnionImageViewer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ONION_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/OnionImageViewer.png")));

    // "Open with" from Explorer, or a path dragged onto the .exe.
    const QStringList arguments = QCoreApplication::arguments();

    // Preferences > Startup can send the file to a window that is already open instead.
    if (arguments.size() > 1 && QSettings().value(Preferences::OpenInExistingWindow, false).toBool()
        && SingleInstance::forwardToRunning(arguments.at(1))) {
        return 0;
    }

    MainWindow window;
    window.show();
    if (arguments.size() > 1) {
        window.openPath(arguments.at(1));
    }

    return app.exec();
}
