#include "app/Application.h"

#include "ai/AiSender.h"
#include "capture/CaptureController.h"
#include "hotkey/HotkeyManager.h"
#include "i18n/I18n.h"
#include "settings/SettingsController.h"
#include "tray/TrayIcon.h"

#include <QApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace {
void applyDarkTitleBar(QWindow *window)
{
    if (!window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    // #111214 → BGR
    const COLORREF bg = RGB(0x14, 0x12, 0x11);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &bg, sizeof(bg));
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &bg, sizeof(bg));
    const COLORREF text = RGB(0xf2, 0xf2, 0xf2);
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
}
} // namespace
#endif

Application::Application(QObject *parent)
    : QObject(parent)
{
}

bool Application::initialize()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    m_settings = new SettingsController(this);
    I18n::instance().setLanguage(m_settings->language());

    m_hotkeys = new HotkeyManager(this);
    m_capture = new CaptureController(this);
    m_ai = new AiSender(this);
    m_tray = new TrayIcon(this);

    m_engine.rootContext()->setContextProperty(QStringLiteral("settingsController"), m_settings);
    m_engine.rootContext()->setContextProperty(QStringLiteral("appI18n"), &I18n::instance());

    connect(m_tray, &TrayIcon::captureRequested, this, &Application::startCapture);
    connect(m_tray, &TrayIcon::settingsRequested, this, &Application::showSettings);
    connect(m_tray, &TrayIcon::quitRequested, qApp, &QCoreApplication::quit);
    connect(m_hotkeys, &HotkeyManager::activated, this, &Application::startCapture);
    connect(m_settings, &SettingsController::saved, this, &Application::applySettingsFromController);
    connect(m_capture, &CaptureController::aiInquireRequested, this, &Application::onAiInquire);
    connect(m_capture, &CaptureController::captureFinished, this, [this](bool copied) {
        m_busy.store(false, std::memory_order_release);
        if (copied)
            m_tray->notify(I18n::instance().tr(QStringLiteral("notify_copied")));
    });
    connect(m_ai, &AiSender::notify, this, [this](const QString &msg) { m_tray->notify(msg); });
    connect(m_ai, &AiSender::finished, this, [this](bool) {
        m_busy.store(false, std::memory_order_release);
    });
    connect(m_settings, &SettingsController::closed, this, [this]() {
        if (m_settingsWindow)
            m_settingsWindow->close();
    });
    connect(&I18n::instance(), &I18n::languageChanged, this, [this]() {
        m_tray->retranslate(m_settings->hotkeyLabel());
    });

    registerHotkeyFromSettings();
    m_capture->setSettings(m_settings->current());
    m_tray->retranslate(m_settings->hotkeyLabel());
    return true;
}

void Application::registerHotkeyFromSettings()
{
    const QString hk = m_settings->hotkey();
    if (!m_hotkeys->registerHotkey(hk))
        m_hotkeys->registerHotkey(QStringLiteral("alt+a"));
}

void Application::applySettingsFromController()
{
    registerHotkeyFromSettings();
    m_capture->setSettings(m_settings->current());
    m_tray->retranslate(m_settings->hotkeyLabel());
}

void Application::showSettings()
{
    m_settings->reload();
    if (m_settingsWindow) {
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->requestActivate();
#ifdef Q_OS_WIN
        applyDarkTitleBar(m_settingsWindow);
#endif
        return;
    }

    const QUrl url(QStringLiteral("qrc:/qt/qml/CheetahClip/qml/SettingsWindow.qml"));
    QObject::connect(
        &m_engine, &QQmlApplicationEngine::objectCreated, this,
        [this](QObject *obj, const QUrl &objUrl) {
            if (!obj && objUrl.path().endsWith(QLatin1String("SettingsWindow.qml")))
                return;
            if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
                m_settingsWindow = win;
                QObject::connect(win, &QObject::destroyed, this, [this]() { m_settingsWindow = nullptr; });
                win->show();
                win->requestActivate();
#ifdef Q_OS_WIN
                applyDarkTitleBar(win);
                // 再刷一次，确保 DWM 生效
                QTimer::singleShot(0, win, [win]() { applyDarkTitleBar(win); });
#endif
            }
        },
        Qt::SingleShotConnection);
    m_engine.load(url);
}

void Application::startCapture()
{
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    m_settings->reload();
    m_capture->setSettings(m_settings->current());
    m_capture->startCapture();
}

void Application::onAiInquire(const QPixmap &image)
{
    // 发送期间保持 busy（captureFinished 可能先清掉）
    m_busy.store(true, std::memory_order_release);
    m_tray->notify(I18n::instance().tr(QStringLiteral("notify_sending")));
    m_ai->sendAsync(m_settings->current(), image);
}
