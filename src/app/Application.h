#pragma once

#include <QObject>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include <atomic>

class SettingsController;
class TrayIcon;
class HotkeyManager;
class CaptureController;
class AiSender;

/** 托盘应用入口：热键、截图、设置与 AI 发送编排。 */
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject* parent = nullptr);
    bool initialize();

public slots:
    void showSettings();
    void startCapture();
    void applySettingsFromController();
    void onAiInquire(const QPixmap& image);

private:
    void registerHotkeyFromSettings();
    void ensureSettingsWindow();
    void finishSettingsWindowSetup(QQuickWindow* win);

    SettingsController* m_settings = nullptr;
    TrayIcon* m_tray = nullptr;
    HotkeyManager* m_hotkeys = nullptr;
    CaptureController* m_capture = nullptr;
    AiSender* m_ai = nullptr;
    std::atomic_bool m_busy{false};
    QQmlApplicationEngine m_engine;
    QQuickWindow* m_settingsWindow = nullptr;
    bool m_settingsLoading = false;
};
