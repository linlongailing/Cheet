#include "app/Application.h"

#include <QApplication>
#include <QQmlEngine>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("cheetahClip"));
    QApplication::setOrganizationName(QStringLiteral("cheetahClip"));
    QApplication::setQuitOnLastWindowClosed(false);

    Application cheetah;
    if (!cheetah.initialize())
        return 1;

    return app.exec();
}
