#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("OnionImageViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("OnionImageViewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/OnionImageViewer.png")));

    MainWindow window;
    window.show();

    // "Open with" from Explorer, or a path dragged onto the .exe.
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1) {
        window.openPath(arguments.at(1));
    }

    return app.exec();
}
