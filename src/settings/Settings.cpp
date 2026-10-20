#include "settings/Settings.h"

#include "ai/SiteCatalog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {
const QSet<QString> kModifiers{QStringLiteral("ctrl"),
                               QStringLiteral("control"),
                               QStringLiteral("alt"),
                               QStringLiteral("shift"),
                               QStringLiteral("windows"),
                               QStringLiteral("win"),
                               QStringLiteral("cmd"),
                               QStringLiteral("command")};
} // namespace

QString Settings::appDataDir() {
    // 优先 %LOCALAPPDATA%（尊重系统重定向），不硬编码用户主目录
    QString dir;
    if (const QByteArray local = qgetenv("LOCALAPPDATA"); !local.isEmpty()) {
        dir = QString::fromLocal8Bit(local) + QStringLiteral("/Cheet");
    } else {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dir.isEmpty())
            dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                  QStringLiteral("/Cheet");
    }
    QDir().mkpath(dir);
    return dir;
}

QString Settings::settingsPath() {
    return appDataDir() + QStringLiteral("/settings.json");
}

Settings Settings::load() {
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly))
        return Settings{};
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return Settings{};
    return fromJson(doc.object());
}

bool Settings::save() const {
    // QSaveFile 原子落盘：进程中途崩溃也不会把既有 settings.json 截断成坏文件
    QSaveFile f(settingsPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const QByteArray data = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (f.write(data) != data.size())
        return false;
    return f.commit();
}

QString Settings::normalizedHotkey() const {
    QString hk = hotkey.trimmed().toLower();
    if (hk.isEmpty())
        hk = QStringLiteral("alt+a");
    const auto parts = hk.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    bool hasMod = false;
    for (const auto& p : parts) {
        if (kModifiers.contains(p.trimmed())) {
            hasMod = true;
            break;
        }
    }
    return hasMod ? hk : QStringLiteral("alt+a");
}

QString Settings::hotkeyLabel() const {
    // Title-case each part simply
    QStringList titled;
    for (QString p : normalizedHotkey().split(QLatin1Char('+'))) {
        p = p.trimmed();
        if (!p.isEmpty())
            titled << p.left(1).toUpper() + p.mid(1);
    }
    return titled.join(QStringLiteral(" + "));
}

QString Settings::normalizedBrowser() const {
    return SiteCatalog::normalizeBrowser(browser);
}

QString Settings::normalizedSite() const {
    return SiteCatalog::normalizeSite(targetSite);
}

QString Settings::normalizedLanguage() const {
    const QString l = language.trimmed().toLower();
    return (l == QLatin1String("en")) ? QStringLiteral("en") : QStringLiteral("zh");
}

QString Settings::targetUrl() const {
    return SiteCatalog::targetUrl(normalizedSite());
}

QJsonObject Settings::toJson() const {
    QJsonObject o;
    o.insert(QStringLiteral("select_mode"), selectMode);
    o.insert(QStringLiteral("fixed_width"), fixedWidth);
    o.insert(QStringLiteral("fixed_height"), fixedHeight);
    o.insert(QStringLiteral("paste_wait_seconds"), pasteWaitSeconds);
    o.insert(QStringLiteral("prompt_text"), promptText);
    o.insert(QStringLiteral("hotkey"), normalizedHotkey());
    o.insert(QStringLiteral("browser"), normalizedBrowser());
    o.insert(QStringLiteral("target_site"), normalizedSite());
    o.insert(QStringLiteral("use_desktop_client"), useDesktopClient);
    o.insert(QStringLiteral("start_with_windows"), startWithWindows);
    o.insert(QStringLiteral("language"), normalizedLanguage());
    return o;
}

Settings Settings::fromJson(const QJsonObject& obj) {
    Settings s;
    if (obj.contains(QStringLiteral("select_mode")))
        s.selectMode = obj.value(QStringLiteral("select_mode")).toString(s.selectMode);
    // 数值一律在加载边界钳制：settings.json 被手改/损坏成极端值时，
    // 后续 static_cast<int>(seconds*1000) 等换算不会溢出，固定选区尺寸也不会为负
    if (obj.contains(QStringLiteral("fixed_width")))
        s.fixedWidth =
            qBound(8, obj.value(QStringLiteral("fixed_width")).toInt(s.fixedWidth), 16384);
    if (obj.contains(QStringLiteral("fixed_height")))
        s.fixedHeight =
            qBound(8, obj.value(QStringLiteral("fixed_height")).toInt(s.fixedHeight), 16384);
    if (obj.contains(QStringLiteral("paste_wait_seconds")))
        s.pasteWaitSeconds =
            qBound(0.0,
                   obj.value(QStringLiteral("paste_wait_seconds")).toDouble(s.pasteWaitSeconds),
                   60.0);
    if (obj.contains(QStringLiteral("prompt_text")))
        s.promptText = obj.value(QStringLiteral("prompt_text")).toString(s.promptText);
    if (obj.contains(QStringLiteral("hotkey")))
        s.hotkey = obj.value(QStringLiteral("hotkey")).toString(s.hotkey);
    if (obj.contains(QStringLiteral("browser")))
        s.browser = obj.value(QStringLiteral("browser")).toString(s.browser);
    if (obj.contains(QStringLiteral("target_site")))
        s.targetSite = obj.value(QStringLiteral("target_site")).toString(s.targetSite);
    if (obj.contains(QStringLiteral("use_desktop_client")))
        s.useDesktopClient =
            obj.value(QStringLiteral("use_desktop_client")).toBool(s.useDesktopClient);
    if (obj.contains(QStringLiteral("start_with_windows")))
        s.startWithWindows =
            obj.value(QStringLiteral("start_with_windows")).toBool(s.startWithWindows);
    if (obj.contains(QStringLiteral("language")))
        s.language = obj.value(QStringLiteral("language")).toString(s.language);
    return s;
}
