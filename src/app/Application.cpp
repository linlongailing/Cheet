#include "app/Application.h"

#include "ai/AiSender.h"
#include "capture/CaptureController.h"
#include "hotkey/HotkeyManager.h"
#include "i18n/I18n.h"
#include "settings/SettingsController.h"
#include "tray/TrayIcon.h"

#include <QApplication>
#include <QDebug>
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
            m_settingsWindow->hide();
    });
    connect(&I18n::instance(), &I18n::languageChanged, this, [this]() {
        m_tray->retranslate(m_settings->hotkeyLabel());
    });

    registerHotkeyFromSettings();
    m_capture->setSettings(m_settings->current());
    m_tray->retranslate(m_settings->hotkeyLabel());

    // 空闲时预加载设置页，避免首次打开卡顿/闪白
    QTimer::singleShot(0, this, [this]() { ensureSettingsWindow(); });
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

void Application::finishSettingsWindowSetup(QQuickWindow *win)
{
    if (!win)
        return;
    if (m_settingsWindow == win)
        return;
    m_settingsWindow = win;
    QObject::connect(win, &QObject::destroyed, this, [this]() { m_settingsWindow = nullptr; });
    // 先铺深色底，再套 DWM，避免先闪系统默认浅色标题栏
    win->setColor(QColor(0x11, 0x12, 0x14));
#ifdef Q_OS_WIN
    // 创建原生窗口句柄后再设暗色标题栏（仍保持隐藏）
    win->create();
    applyDarkTitleBar(win);
#endif
}

void Application::ensureSettingsWindow()
{
    if (m_settingsWindow || m_settingsLoading)
        return;
    m_settingsLoading = true;

    const QUrl url(QStringLiteral("qrc:/qt/qml/CheetahClip/qml/SettingsWindow.qml"));
    // 不用 SingleShot：objectCreated 可能先报其它对象，匹配到设置窗再断开
    QMetaObject::Connection *conn = new QMetaObject::Connection;
    *conn = QObject::connect(
        &m_engine, &QQmlApplicationEngine::objectCreated, this,
        [this, conn](QObject *obj, const QUrl &objUrl) {
            if (!objUrl.path().endsWith(QLatin1String("SettingsWindow.qml")))
                return;
            QObject::disconnect(*conn);
            delete conn;
            m_settingsLoading = false;
            if (!obj)
                return;
            if (auto *win = qobject_cast<QQuickWindow *>(obj))
                finishSettingsWindowSetup(win);
        });
    m_engine.load(url);
}

void Application::showSettings()
{
    m_settings->reload();
    ensureSettingsWindow();

    auto present = [this]() {
        if (!m_settingsWindow)
            return;
#ifdef Q_OS_WIN
        applyDarkTitleBar(m_settingsWindow);
#endif
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->requestActivate();
#ifdef Q_OS_WIN
        applyDarkTitleBar(m_settingsWindow);
#endif
    };

    if (m_settingsWindow) {
        present();
        return;
    }

    // 预加载尚未完成时，等窗口创建好再显示
    QMetaObject::Connection *conn = new QMetaObject::Connection;
    *conn = QObject::connect(
        &m_engine, &QQmlApplicationEngine::objectCreated, this,
        [this, present, conn](QObject *obj, const QUrl &objUrl) {
            if (!obj || !objUrl.path().endsWith(QLatin1String("SettingsWindow.qml")))
                return;
            QObject::disconnect(*conn);
            delete conn;
            if (auto *win = qobject_cast<QQuickWindow *>(obj)) {
                finishSettingsWindowSetup(win);
                present();
            }
        });
}

void Application::startCapture()
{
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    // 使用内存中的当前设置，勿 reload：否则会冲掉未点「保存」的客户端开关等改动
    m_capture->setSettings(m_settings->current());
    m_capture->startCapture();
}

void Application::onAiInquire(const QPixmap &image)
{
    // 发送期间保持 busy（captureFinished 可能先清掉）
    m_busy.store(true, std::memory_order_release);
    m_tray->notify(I18n::instance().tr(QStringLiteral("notify_sending")));
    const Settings s = m_settings->current();
    qDebug("AiSender route: site=%s useDesktop=%d", qPrintable(s.normalizedSite()),
           int(s.useDesktopClient));
    m_ai->sendAsync(s, image);
}
