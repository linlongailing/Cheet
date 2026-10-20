#include "app/Application.h"
#include "app/SingleInstance.h"

#include <QApplication>
#include <QQmlEngine>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Cheet"));
    QApplication::setOrganizationName(QStringLiteral("Cheet"));
    QApplication::setQuitOnLastWindowClosed(false);

    SingleInstance guard;
    if (!guard.tryRunAsPrimary())
        return 0; // 已有实例，已请求激活

    Application appCore;
    if (!appCore.initialize())
        return 1;
    QObject::connect(
        &guard, &SingleInstance::activateRequested, &appCore, &Application::showSettings);

    return app.exec();
}
