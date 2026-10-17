#include "ai/AiSender.h"

#include "ai/BrowserNav.h"
#include "ai/SiteCatalog.h"
#include "ai/UiaComposer.h"
#include "ai/WinInput.h"
#include "i18n/I18n.h"
#include "platform/ComInit.h"
#include "platform/UiaCore.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

AiSender::AiSender(QObject *parent)
    : QObject(parent)
{
}

void AiSender::emitNotify(const QString &msg)
{
    QMetaObject::invokeMethod(this, [this, msg]() { emit notify(msg); }, Qt::QueuedConnection);
}

QString AiSender::saveTempImage(const QPixmap &image) const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty() || image.isNull())
        return {};
    QDir().mkpath(dir);
    // 唯一文件名，避免并发覆盖；不嵌入用户路径信息
    const QString path =
        dir + QStringLiteral("/cheetahClip_%1.png")
                  .arg(QDateTime::currentMSecsSinceEpoch());
    if (!image.save(path, "PNG")) {
        qWarning("AiSender: temp image save failed");
        return {};
    }
    return path;
}

void AiSender::sendAsync(const Settings &settings, const QPixmap &image)
{
    saveTempImage(image);
    QApplication::clipboard()->setPixmap(image);

    const QPointer<AiSender> self(this);
    QtConcurrent::run([self, settings]() {
        if (!self)
            return;
        const bool ok = self->sendSync(settings);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, ok]() {
            if (self)
                emit self->finished(ok);
        }, Qt::QueuedConnection);
    });
}

bool AiSender::sendSync(const Settings &settings)
{
#ifdef Q_OS_WIN
    platform::ComApartment comApt;
    if (!comApt.ok()) {
        qWarning("AiSender: COM apartment init failed");
        return false;
    }

    auto &i18n = I18n::instance();
    const QString site = settings.normalizedSite();
    const QString siteLabel = SiteCatalog::siteLabel(site);

    // 启用桌面客户端：只走客户端，绝不回退浏览器
    if (settings.useDesktopClient) {
        if (!WinInput::isDesktopInstalled(site)) {
            emitNotify(i18n.tr(QStringLiteral("notify_desktop_missing"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }

        emitNotify(i18n.tr(QStringLiteral("notify_opening"), {{QStringLiteral("label"), siteLabel}}));
        HWND hwnd = WinInput::findDesktopWindow(site);
        if (!hwnd) {
            if (!WinInput::launchDesktop(site)) {
                emitNotify(i18n.tr(QStringLiteral("notify_desktop_stopped"),
                                   {{QStringLiteral("site"), siteLabel}}));
                return false;
            }
            for (int i = 0; i < 40 && !hwnd; ++i) {
                WinInput::sleepMs(500);
                hwnd = WinInput::findDesktopWindow(site);
            }
        }
        if (!platform::windowAlive(hwnd)) {
            emitNotify(i18n.tr(QStringLiteral("notify_desktop_stopped"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }

        WinInput::setForeground(hwnd);
        WinInput::sleepMs(1000);
        if (!platform::windowAlive(hwnd)) {
            hwnd = WinInput::findDesktopWindow(site);
            if (!platform::windowAlive(hwnd)) {
                emitNotify(i18n.tr(QStringLiteral("notify_desktop_stopped"),
                                   {{QStringLiteral("site"), siteLabel}}));
                return false;
            }
        }

        if (pastePromptAndSend(settings, reinterpret_cast<quintptr>(hwnd), true)) {
            emitNotify(i18n.tr(QStringLiteral("notify_sent"), {{QStringLiteral("site"), siteLabel}}));
            return true;
        }
        emitNotify(i18n.tr(QStringLiteral("notify_desktop_stopped"),
                           {{QStringLiteral("site"), siteLabel}}));
        return false;
    }

    // 未启用桌面客户端：走浏览器
    const QString browser = settings.normalizedBrowser();
    const QString url = settings.targetUrl();
    if (WinInput::findBrowserExe(browser).isEmpty() && !WinInput::findBrowserWindow(browser)) {
        emitNotify(i18n.tr(QStringLiteral("notify_browser_missing"),
                           {{QStringLiteral("browser"), browser}}));
        return false;
    }

    emitNotify(i18n.tr(QStringLiteral("notify_opening"),
                       {{QStringLiteral("label"), browser + QLatin1Char(' ') + siteLabel}}));

    bool launched = false;
    HWND hwnd = BrowserNav::focusOrOpenTarget(browser, site, url, &launched);
    if (!platform::windowAlive(hwnd)) {
        emitNotify(i18n.tr(QStringLiteral("notify_open_failed"),
                           {{QStringLiteral("label"), browser}}));
        return false;
    }

    WinInput::setForeground(hwnd);
    WinInput::sleepMs(launched ? 2000 : 600);
    if (!platform::windowAlive(hwnd)) {
        hwnd = WinInput::findBrowserWindow(browser);
        if (!platform::windowAlive(hwnd)) {
            emitNotify(i18n.tr(QStringLiteral("notify_stopped"),
                               {{QStringLiteral("browser"), browser}}));
            return false;
        }
    }

    if (pastePromptAndSend(settings, reinterpret_cast<quintptr>(hwnd), false)) {
        emitNotify(i18n.tr(QStringLiteral("notify_sent"), {{QStringLiteral("site"), siteLabel}}));
        return true;
    }
    emitNotify(i18n.tr(QStringLiteral("notify_stopped"), {{QStringLiteral("browser"), browser}}));
    return false;
#else
    Q_UNUSED(settings);
    return false;
#endif
}

bool AiSender::pastePromptAndSend(const Settings &settings, quintptr hwndValue, bool desktopMode)
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    if (!platform::windowAlive(hwnd))
        return false;

    const QString site = settings.normalizedSite();
    const QString browser = settings.normalizedBrowser();

    auto refreshAlive = [&]() -> bool {
        if (platform::windowAlive(hwnd))
            return true;
        hwnd = desktopMode ? WinInput::findDesktopWindow(site) : WinInput::findBrowserWindow(browser);
        return platform::windowAlive(hwnd);
    };

    auto abortIfGone = [&]() -> bool {
        if (refreshAlive())
            return false;
        qWarning("AiSender abort: target window closed");
        return true;
    };

    UiaComposer::enableWebAccessibility(hwnd);
    WinInput::setForeground(hwnd);

    // 等输入框出现（页面就绪）
    {
        const qint64 deadline = GetTickCount64() + 30000;
        while (GetTickCount64() < deadline) {
            if (abortIfGone())
                return false;
            if (UiaComposer::focusComposer(hwnd, site))
                break;
            WinInput::sleepMs(400);
        }
    }
    if (abortIfGone())
        return false;

    UiaComposer::clearComposer(hwnd, site);
    if (abortIfGone())
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sleepMs(200);

    bool attached = false;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (abortIfGone())
            return false;
        const auto before = UiaComposer::scan(hwnd, site).attachments;
        if (!UiaComposer::focusComposer(hwnd, site)) {
            RECT r{};
            if (GetWindowRect(hwnd, &r) && r.right > r.left && r.bottom > r.top) {
                const int cx = r.left + static_cast<int>((r.right - r.left) * 0.50);
                const int cy = r.top + static_cast<int>((r.bottom - r.top) * 0.92);
                SetCursorPos(cx, cy);
                INPUT ins[2]{};
                ins[0].type = INPUT_MOUSE;
                ins[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                ins[1].type = INPUT_MOUSE;
                ins[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(2, ins, sizeof(INPUT));
            }
        }
        WinInput::sleepMs(200);
        if (abortIfGone())
            return false;
        WinInput::sendCtrlKey('V');
        if (!UiaComposer::waitAttachmentAdded(hwnd, site, before, 5000)) {
            if (abortIfGone())
                return false;
            continue;
        }
        attached = true;
        break;
    }
    if (abortIfGone())
        return false;
    if (!attached)
        qWarning("attachment not confirmed, continue with upload wait");

    QString prompt = settings.promptText.trimmed();
    if (prompt.isEmpty())
        prompt = I18n::defaultPrompt(settings.normalizedLanguage());

    QMetaObject::invokeMethod(
        qApp, [prompt]() { QApplication::clipboard()->setText(prompt); }, Qt::BlockingQueuedConnection);
    WinInput::sleepMs(120);
    if (abortIfGone())
        return false;
    UiaComposer::focusComposer(hwnd, site);
    WinInput::sleepMs(150);
    if (abortIfGone())
        return false;
    WinInput::sendCtrlKey('V');
    WinInput::sleepMs(300);
    if (abortIfGone())
        return false;

    const int minWait = qMax(1000, static_cast<int>(settings.pasteWaitSeconds * 1000));
    if (!UiaComposer::waitUntilUploaded(hwnd, site, minWait, 120000)) {
        if (abortIfGone())
            return false;
    }
    if (abortIfGone())
        return false;

    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (abortIfGone())
            return false;
        UiaComposer::focusComposer(hwnd, site);
        WinInput::setForeground(hwnd);
        WinInput::sleepMs(150);
        if (abortIfGone())
            return false;
        WinInput::sendEnter();
        WinInput::sleepMs(1500);
        if (abortIfGone())
            return false;
        if (UiaComposer::messageSent(hwnd, site, prompt))
            return true;
        if (!UiaComposer::waitUntilUploaded(hwnd, site, 1000, 60000)) {
            if (abortIfGone())
                return false;
        }
    }
    // 未能确认发送成功时一律失败，避免窗口仍在却误报成功
    return false;
#else
    Q_UNUSED(settings);
    Q_UNUSED(hwndValue);
    Q_UNUSED(desktopMode);
    return false;
#endif
}
