#include "settings/SettingsController.h"

#include "ai/WinInput.h"
#include "autostart/Autostart.h"
#include "i18n/I18n.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

bool browserInstalled(const QString &key)
{
    // 简化检测：注册表 + 常见路径
    if (key == QLatin1String("edge")) {
        QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe"),
                      QSettings::NativeFormat);
        const QString p = reg.value(QStringLiteral(".")).toString();
        if (!p.isEmpty() && QFileInfo::exists(p))
            return true;
    } else if (key == QLatin1String("chrome")) {
        QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe"),
                      QSettings::NativeFormat);
        const QString p = reg.value(QStringLiteral(".")).toString();
        if (!p.isEmpty() && QFileInfo::exists(p))
            return true;
    } else if (key == QLatin1String("firefox")) {
        QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\firefox.exe"),
                      QSettings::NativeFormat);
        const QString p = reg.value(QStringLiteral(".")).toString();
        if (!p.isEmpty() && QFileInfo::exists(p))
            return true;
    }
    return false;
}

bool desktopInstalled(const QString &site)
{
#ifdef Q_OS_WIN
    return WinInput::isDesktopInstalled(site);
#else
    Q_UNUSED(site);
    return false;
#endif
}

} // namespace

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    reload();
}

void SettingsController::reload()
{
    m_settings = Settings::load();
    m_settings.startWithWindows = Autostart::isEnabled();
    m_original = m_settings;
    refreshDesktopHint();
    emit languageChanged();
    emit hotkeyChanged();
    emit selectModeChanged();
    emit fixedWidthChanged();
    emit fixedHeightChanged();
    emit browserChanged();
    emit targetSiteChanged();
    emit useDesktopClientChanged();
    emit promptTextChanged();
    emit startWithWindowsChanged();
    emit browserOptionsChanged();
}

void SettingsController::setLanguage(const QString &v)
{
    if (m_settings.language == v)
        return;
    const QString old = m_settings.normalizedLanguage();
    m_settings.language = v;
    if (m_settings.promptText == I18n::defaultPrompt(old))
        m_settings.promptText = I18n::defaultPrompt(m_settings.normalizedLanguage());
    I18n::instance().setLanguage(m_settings.normalizedLanguage());
    refreshDesktopHint();
    emit languageChanged();
    emit promptTextChanged();
    emit browserOptionsChanged();
    emit desktopHintChanged();
}

void SettingsController::setHotkey(const QString &v)
{
    if (m_settings.hotkey == v)
        return;
    m_settings.hotkey = v;
    emit hotkeyChanged();
}

void SettingsController::setSelectMode(const QString &v)
{
    if (m_settings.selectMode == v)
        return;
    m_settings.selectMode = v;
    emit selectModeChanged();
}

void SettingsController::setFixedWidth(int v)
{
    v = qBound(v, 50, 7680);
    if (m_settings.fixedWidth == v)
        return;
    m_settings.fixedWidth = v;
    emit fixedWidthChanged();
}

void SettingsController::setFixedHeight(int v)
{
    v = qBound(v, 50, 4320);
    if (m_settings.fixedHeight == v)
        return;
    m_settings.fixedHeight = v;
    emit fixedHeightChanged();
}

void SettingsController::setBrowser(const QString &v)
{
    if (m_settings.browser == v)
        return;
    m_settings.browser = v;
    emit browserChanged();
}

void SettingsController::setTargetSite(const QString &v)
{
    if (m_settings.targetSite == v)
        return;
    m_settings.targetSite = v;
    emit targetSiteChanged();
    refreshDesktopHint();
}

void SettingsController::setUseDesktopClient(bool v)
{
    if (m_settings.useDesktopClient == v)
        return;
    m_settings.useDesktopClient = v;
    emit useDesktopClientChanged();
    refreshDesktopHint();
}

void SettingsController::setPromptText(const QString &v)
{
    if (m_settings.promptText == v)
        return;
    m_settings.promptText = v;
    emit promptTextChanged();
}

void SettingsController::setStartWithWindows(bool v)
{
    if (m_settings.startWithWindows == v)
        return;
    m_settings.startWithWindows = v;
    emit startWithWindowsChanged();
}

QVariantList SettingsController::siteOptions() const
{
    QVariantList list;
    for (const auto &p : {std::pair{QStringLiteral("chatgpt"), QStringLiteral("ChatGPT")},
                          {QStringLiteral("gemini"), QStringLiteral("Gemini")},
                          {QStringLiteral("claude"), QStringLiteral("Claude")}}) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), p.first);
        m.insert(QStringLiteral("label"), p.second);
        list.push_back(m);
    }
    return list;
}

QVariantList SettingsController::browserOptions() const
{
    QVariantList list;
    const QList<QPair<QString, QString>> items{
        {QStringLiteral("edge"), QStringLiteral("Microsoft Edge")},
        {QStringLiteral("chrome"), QStringLiteral("Google Chrome")},
        {QStringLiteral("firefox"), QStringLiteral("Mozilla Firefox")},
    };
    for (const auto &p : items) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), p.first);
        QString label = p.second;
        if (!browserInstalled(p.first))
            label = I18n::instance().tr(QStringLiteral("not_detected"), {{QStringLiteral("label"), p.second}});
        m.insert(QStringLiteral("label"), label);
        list.push_back(m);
    }
    return list;
}

QVariantList SettingsController::modeOptions() const
{
    QVariantList list;
    QVariantMap freeMode;
    freeMode.insert(QStringLiteral("key"), QStringLiteral("free"));
    freeMode.insert(QStringLiteral("label"), I18n::instance().tr(QStringLiteral("mode_free")));
    list.push_back(freeMode);
    QVariantMap fixedMode;
    fixedMode.insert(QStringLiteral("key"), QStringLiteral("fixed"));
    fixedMode.insert(QStringLiteral("label"), I18n::instance().tr(QStringLiteral("mode_fixed")));
    list.push_back(fixedMode);
    return list;
}

QVariantList SettingsController::languageOptions() const
{
    QVariantList list;
    QVariantMap zh;
    zh.insert(QStringLiteral("key"), QStringLiteral("zh"));
    zh.insert(QStringLiteral("label"), QStringLiteral("简体中文"));
    list.push_back(zh);
    QVariantMap en;
    en.insert(QStringLiteral("key"), QStringLiteral("en"));
    en.insert(QStringLiteral("label"), QStringLiteral("English"));
    list.push_back(en);
    return list;
}

void SettingsController::resetHotkey()
{
    setHotkey(QStringLiteral("alt+a"));
}

bool SettingsController::recordHotkeyFromKeys(const QStringList &modifiers, const QString &key)
{
    if (key.isEmpty() || modifiers.isEmpty())
        return false;
    QStringList parts = modifiers;
    parts << key.toLower();
    setHotkey(parts.join(QLatin1Char('+')));
    return true;
}

bool SettingsController::save()
{
    if (!Autostart::setEnabled(m_settings.startWithWindows))
        return false;
    if (m_settings.promptText.trimmed().isEmpty())
        m_settings.promptText = I18n::defaultPrompt(m_settings.normalizedLanguage());
    if (!m_settings.save())
        return false;
    m_original = m_settings;
    emit saved();
    emit closed();
    return true;
}

void SettingsController::cancel()
{
    m_settings = m_original;
    emit closed();
}

void SettingsController::refreshDesktopHint()
{
    auto &i18n = I18n::instance();
    const QString site = m_settings.normalizedSite();
    QString siteName = site;
    if (site == QLatin1String("chatgpt"))
        siteName = QStringLiteral("ChatGPT");
    else if (site == QLatin1String("gemini"))
        siteName = QStringLiteral("Gemini");
    else if (site == QLatin1String("claude"))
        siteName = QStringLiteral("Claude");

    if (!m_settings.useDesktopClient) {
        m_desktopHint = i18n.tr(QStringLiteral("desktop_hint_off"));
        m_desktopHintColor = QStringLiteral("#5c5c5c");
    } else if (desktopInstalled(site)) {
        m_desktopHint = i18n.tr(QStringLiteral("desktop_hint_found"), {{QStringLiteral("site"), siteName}});
        m_desktopHintColor = QStringLiteral("#6aa8ff");
    } else {
        m_desktopHint = i18n.tr(QStringLiteral("desktop_hint_missing"), {{QStringLiteral("site"), siteName}});
        m_desktopHintColor = QStringLiteral("#e0a050");
    }
    emit desktopHintChanged();
}
