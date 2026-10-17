#include "app/Application.h"

#include <QApplication>
#include <QQmlEngine>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Cheet"));
    QApplication::setOrganizationName(QStringLiteral("Cheet"));
    QApplication::setQuitOnLastWindowClosed(false);

    Application appCore;
    if (!appCore.initialize())
        return 1;

    return app.exec();
}
