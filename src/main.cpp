#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("OnionImageViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("OnionImageViewer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/OnionImageViewer.png")));

    MainWindow window;
    window.show();

    return app.exec();
}
