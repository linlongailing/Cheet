#pragma once

#include <QJsonObject>
#include <QString>

/** 应用设置（%LOCALAPPDATA%/Cheet/settings.json）。 */
class Settings {
public:
    QString selectMode = QStringLiteral("free");
    int fixedWidth = 800;
    int fixedHeight = 600;
    QString promptText = QStringLiteral("详解图片内容");
    QString hotkey = QStringLiteral("alt+a");
    QString browser = QStringLiteral("edge");
    QString targetSite = QStringLiteral("chatgpt");
    bool useDesktopClient = false;
    bool startWithWindows = false;
    QString language = QStringLiteral("zh");

    static QString appDataDir();
    static QString settingsPath();

    static Settings load();
    bool save() const;

    QString normalizedHotkey() const;
    QString hotkeyLabel() const;
    QString normalizedBrowser() const;
    QString normalizedSite() const;
    QString normalizedLanguage() const;
    QString targetUrl() const;

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject& obj);
};
