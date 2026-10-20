#pragma once

#include "settings/Settings.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

/** 暴露给 QML 的设置控制器。 */
class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString hotkey READ hotkey WRITE setHotkey NOTIFY hotkeyChanged)
    Q_PROPERTY(QString hotkeyLabel READ hotkeyLabel NOTIFY hotkeyChanged)
    Q_PROPERTY(QString selectMode READ selectMode WRITE setSelectMode NOTIFY selectModeChanged)
    Q_PROPERTY(int fixedWidth READ fixedWidth WRITE setFixedWidth NOTIFY fixedWidthChanged)
    Q_PROPERTY(int fixedHeight READ fixedHeight WRITE setFixedHeight NOTIFY fixedHeightChanged)
    Q_PROPERTY(QString browser READ browser WRITE setBrowser NOTIFY browserChanged)
    Q_PROPERTY(QString targetSite READ targetSite WRITE setTargetSite NOTIFY targetSiteChanged)
    Q_PROPERTY(bool useDesktopClient READ useDesktopClient WRITE setUseDesktopClient NOTIFY
                   useDesktopClientChanged)
    Q_PROPERTY(QString promptText READ promptText WRITE setPromptText NOTIFY promptTextChanged)
    Q_PROPERTY(bool startWithWindows READ startWithWindows WRITE setStartWithWindows NOTIFY
                   startWithWindowsChanged)
    Q_PROPERTY(QString desktopHint READ desktopHint NOTIFY desktopHintChanged)
    Q_PROPERTY(QString desktopHintColor READ desktopHintColor NOTIFY desktopHintChanged)
    Q_PROPERTY(QVariantList siteOptions READ siteOptions CONSTANT)
    Q_PROPERTY(QVariantList browserOptions READ browserOptions NOTIFY browserOptionsChanged)
    Q_PROPERTY(QVariantList modeOptions READ modeOptions NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languageOptions READ languageOptions CONSTANT)

public:
    explicit SettingsController(QObject* parent = nullptr);

    void reload();
    Settings current() const { return m_settings; }

    QString language() const { return m_settings.normalizedLanguage(); }
    void setLanguage(const QString& v);

    QString hotkey() const { return m_settings.normalizedHotkey(); }
    void setHotkey(const QString& v);
    QString hotkeyLabel() const { return m_settings.hotkeyLabel(); }

    QString selectMode() const { return m_settings.selectMode; }
    void setSelectMode(const QString& v);

    int fixedWidth() const { return m_settings.fixedWidth; }
    void setFixedWidth(int v);
    int fixedHeight() const { return m_settings.fixedHeight; }
    void setFixedHeight(int v);

    QString browser() const { return m_settings.normalizedBrowser(); }
    void setBrowser(const QString& v);

    QString targetSite() const { return m_settings.normalizedSite(); }
    void setTargetSite(const QString& v);

    bool useDesktopClient() const { return m_settings.useDesktopClient; }
    void setUseDesktopClient(bool v);

    QString promptText() const { return m_settings.promptText; }
    void setPromptText(const QString& v);

    bool startWithWindows() const { return m_settings.startWithWindows; }
    void setStartWithWindows(bool v);

    QString desktopHint() const { return m_desktopHint; }
    QString desktopHintColor() const { return m_desktopHintColor; }

    QVariantList siteOptions() const;
    QVariantList browserOptions() const;
    QVariantList modeOptions() const;
    QVariantList languageOptions() const;

    Q_INVOKABLE void resetHotkey();
    Q_INVOKABLE bool recordHotkeyFromKeys(const QStringList& modifiers, const QString& key);
    Q_INVOKABLE bool save();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refreshDesktopHint();

signals:
    void languageChanged();
    void hotkeyChanged();
    void selectModeChanged();
    void fixedWidthChanged();
    void fixedHeightChanged();
    void browserChanged();
    void targetSiteChanged();
    void useDesktopClientChanged();
    void promptTextChanged();
    void startWithWindowsChanged();
    void desktopHintChanged();
    void browserOptionsChanged();
    void saved();
    void closed();

private:
    Settings m_settings;
    Settings m_original;
    QString m_desktopHint;
    QString m_desktopHintColor = QStringLiteral("#5c5c5c");
};
