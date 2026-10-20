#include "autostart/Autostart.h"

#include <QCoreApplication>
#include <QSettings>

namespace {
constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kAppName = "Cheet";
} // namespace

bool Autostart::isEnabled() {
    QSettings reg(QLatin1String(kRunKey), QSettings::NativeFormat);
    return !reg.value(QLatin1String(kAppName)).toString().isEmpty();
}

bool Autostart::setEnabled(bool enabled) {
    QSettings reg(QLatin1String(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        const QString exe =
            QCoreApplication::applicationFilePath().replace(QLatin1Char('/'), QLatin1Char('\\'));
        reg.setValue(QLatin1String(kAppName), QStringLiteral("\"%1\"").arg(exe));
    } else {
        reg.remove(QLatin1String(kAppName));
    }
    reg.sync();
    return reg.status() == QSettings::NoError;
}
