#include "ai/AiSender.h"

#include "ai/BrowserNav.h"
#include "ai/SiteCatalog.h"
#include "ai/UiaComposer.h"
#include "ai/WinInput.h"
#include "capture/ClipboardImage.h"
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
        dir + QStringLiteral("/Cheet_%1.png")
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
    ClipboardImage::copyForPaste(image);

    const QPointer<AiSender> self(this);
    // 把截图带进工作线程：导航若误清剪贴板，发送前可恢复
    QtConcurrent::run([self, settings, image]() {
        if (!self)
            return;
        const bool ok = self->sendSync(settings, image);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, ok]() {
            if (self)
                emit self->finished(ok);
        }, Qt::QueuedConnection);
    });
}

bool AiSender::sendSync(const Settings &settings, const QPixmap &image)
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

    // 导航可能误清剪贴板：贴图前在 GUI 线程恢复截图
    auto restoreImage = [&image](bool web) {
        QMetaObject::invokeMethod(
            qApp,
            [&image, web]() {
                if (web)
                    ClipboardImage::copyForWebPaste(image);
                else
                    ClipboardImage::copyForPaste(image);
            },
            Qt::BlockingQueuedConnection);
    };

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
        // Chromium 壳（豆包等）冷启动后 UIA Document 出现较慢
        const int warmMs = SiteCatalog::needsSimplePaste(site) ? 2500 : 1000;
        WinInput::sleepMs(warmMs);
        if (!platform::windowAlive(hwnd)) {
            hwnd = WinInput::findDesktopWindow(site);
            if (!platform::windowAlive(hwnd)) {
                emitNotify(i18n.tr(QStringLiteral("notify_desktop_stopped"),
                                   {{QStringLiteral("site"), siteLabel}}));
                return false;
            }
        }

        restoreImage(false);
        if (pastePromptAndSend(settings, reinterpret_cast<quintptr>(hwnd), true, image)) {
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
    // 新开标签 / 冷启动：多等页面与输入框就绪
    const int pageWait = SiteCatalog::needsSimplePaste(site)
                             ? (launched ? 5500 : 2000)
                             : (launched ? 3000 : 600);
    WinInput::sleepMs(pageWait);
    if (!platform::windowAlive(hwnd)) {
        hwnd = WinInput::findBrowserWindow(browser);
        if (!platform::windowAlive(hwnd)) {
            emitNotify(i18n.tr(QStringLiteral("notify_stopped"),
                               {{QStringLiteral("browser"), browser}}));
            return false;
        }
    }

    restoreImage(true);
    if (pastePromptAndSend(settings, reinterpret_cast<quintptr>(hwnd), false, image)) {
        emitNotify(i18n.tr(QStringLiteral("notify_sent"), {{QStringLiteral("site"), siteLabel}}));
        return true;
    }
    emitNotify(i18n.tr(QStringLiteral("notify_stopped"), {{QStringLiteral("browser"), browser}}));
    return false;
#else
    Q_UNUSED(settings);
    Q_UNUSED(image);
    return false;
#endif
}

bool AiSender::pastePromptAndSend(const Settings &settings, quintptr hwndValue, bool desktopMode,
                                  const QPixmap &image)
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

    auto restoreClipImage = [&]() {
        QMetaObject::invokeMethod(
            qApp,
            [&image, desktopMode]() {
                if (desktopMode)
                    ClipboardImage::copyForPaste(image);
                else
                    ClipboardImage::copyForWebPaste(image);
            },
            Qt::BlockingQueuedConnection);
    };

    UiaComposer::enableWebAccessibility(hwnd);
    WinInput::setForeground(hwnd);

    // 全站：若误进技能/生图等非聊天页，用浏览器直接打开目标聊天 URL
    if (!desktopMode) {
        const QString cur = BrowserNav::readAddressBarUrl(hwnd).toLower();
        bool bad = false;
        for (const QString &h : SiteCatalog::avoidUrlHints(site)) {
            if (cur.contains(h)) {
                bad = true;
                break;
            }
        }
        if (bad) {
            qWarning("AiSender: leave non-chat page for %s", qPrintable(site));
            WinInput::launchBrowser(browser, SiteCatalog::targetUrl(site));
            WinInput::sleepMs(1000);
            hwnd = WinInput::findBrowserWindow(browser);
            if (platform::windowAlive(hwnd))
                BrowserNav::waitAddressBarTarget(hwnd, site, 12000);
            restoreClipImage();
            WinInput::sleepMs(800);
        }
    }

    QString prompt = settings.promptText.trimmed();
    if (prompt.isEmpty())
        prompt = I18n::defaultPrompt(settings.normalizedLanguage());

    auto &i18n = I18n::instance();
    const QString siteLabel = SiteCatalog::siteLabel(site);

    // 贴不上图（未登录 / 套餐限制 / 粘贴失败）：一律停止，不再贴字、不发送
    auto stopAfterImageIssue = [&](UiaComposer::ImagePasteResult r) -> bool {
        if (r == UiaComposer::ImagePasteResult::WindowGone)
            return false;
        if (r == UiaComposer::ImagePasteResult::Blocked) {
            emitNotify(i18n.tr(QStringLiteral("notify_image_blocked"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }
        if (r == UiaComposer::ImagePasteResult::Failed) {
            emitNotify(i18n.tr(QStringLiteral("notify_image_failed"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }
        return true;
    };

    // 等输入框出现（新开标签页尤其需要；豆包等也走同一等待）
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

    // 豆包/千问/DeepSeek：只点聊天输入框（绝不点「图像生成」等技能），再 Ctrl+V 上传
    if (SiteCatalog::needsSimplePaste(site)) {
        if (abortIfGone())
            return false;

        UiaComposer::ImagePasteResult pasteRc = UiaComposer::ImagePasteResult::Failed;
        for (int attempt = 0; attempt < 3 && pasteRc != UiaComposer::ImagePasteResult::Ok
             && pasteRc != UiaComposer::ImagePasteResult::Blocked;
             ++attempt) {
            if (abortIfGone())
                return false;
            restoreClipImage();
            WinInput::setForeground(hwnd);
            if (!desktopMode) {
                WinInput::sendKey(VK_ESCAPE);
                WinInput::sleepMs(120);
            }
            if (!UiaComposer::focusComposer(hwnd, site)) {
                // 聚焦失败等同贴图失败：停止后续贴字/发送
                emitNotify(i18n.tr(QStringLiteral("notify_image_failed"),
                                   {{QStringLiteral("site"), siteLabel}}));
                return false;
            }
            // 第 2 次起：静默点击无效时用真实点击兜底（仅一次闪烁）
            if (!desktopMode && attempt >= 1) {
                RECT rc{};
                if (GetClientRect(hwnd, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
                    POINT pt{};
                    pt.x = rc.left + static_cast<LONG>((rc.right - rc.left) * 0.52);
                    pt.y = rc.top + static_cast<LONG>((rc.bottom - rc.top) * 0.96);
                    if (ClientToScreen(hwnd, &pt))
                        WinInput::clickScreen(static_cast<int>(pt.x), static_cast<int>(pt.y), true);
                }
            }
            WinInput::sleepMs(280);
            if (abortIfGone())
                return false;
            const int before = UiaComposer::probeWindow(hwnd).attachments;
            WinInput::sendCtrlKey('V');
            const int waitMs = qMax(4000, static_cast<int>(settings.pasteWaitSeconds * 1000) + 2000);
            pasteRc = UiaComposer::waitImagePasteResult(hwnd, site, before, waitMs);
            qDebug() << "AiSender simplePaste attempt" << attempt << "rc" << int(pasteRc);
            if (pasteRc == UiaComposer::ImagePasteResult::WindowGone)
                return false;
        }
        if (abortIfGone())
            return false;
        // Failed / Blocked：不贴文字、不回车
        if (!stopAfterImageIssue(pasteRc))
            return false;

        // 贴图后焦点仍在输入框：直接贴字/回车，不再二次点击（避免指针闪烁）
        QMetaObject::invokeMethod(
            qApp, [prompt]() { QApplication::clipboard()->setText(prompt); },
            Qt::BlockingQueuedConnection);
        WinInput::sleepMs(150);
        if (abortIfGone())
            return false;
        WinInput::setForeground(hwnd);
        WinInput::sendCtrlKey('V');
        WinInput::sleepMs(450);
        if (abortIfGone())
            return false;
        WinInput::sendEnter();
        WinInput::sleepMs(1200);
        return platform::windowAlive(hwnd);
    }
    if (abortIfGone())
        return false;

    UiaComposer::clearComposer(hwnd, site);
    if (abortIfGone())
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sleepMs(200);

    UiaComposer::ImagePasteResult pasteRc = UiaComposer::ImagePasteResult::Failed;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (abortIfGone())
            return false;
        restoreClipImage();
        const int before = qMax(UiaComposer::scan(hwnd, site).attachments,
                                UiaComposer::probeWindow(hwnd).attachments);
        if (!UiaComposer::focusComposer(hwnd, site)) {
            RECT r{};
            if (GetWindowRect(hwnd, &r) && r.right > r.left && r.bottom > r.top) {
                const int cx = r.left + static_cast<int>((r.right - r.left) * 0.50);
                const int cy = r.top + static_cast<int>((r.bottom - r.top) * 0.92);
                WinInput::clickScreen(cx, cy, false);
            }
        }
        WinInput::sleepMs(200);
        if (abortIfGone())
            return false;
        WinInput::setForeground(hwnd);
        WinInput::sendCtrlKey('V');
        pasteRc = UiaComposer::waitImagePasteResult(hwnd, site, before, 6000);
        if (abortIfGone())
            return false;
        if (pasteRc == UiaComposer::ImagePasteResult::Blocked)
            break;
        if (pasteRc == UiaComposer::ImagePasteResult::Ok)
            break;
    }
    if (abortIfGone())
        return false;
    if (!stopAfterImageIssue(pasteRc))
        return false;

    // 贴图后不再点输入框，直接贴提示词（指针不闪）
    QMetaObject::invokeMethod(
        qApp, [prompt]() { QApplication::clipboard()->setText(prompt); }, Qt::BlockingQueuedConnection);
    WinInput::sleepMs(120);
    if (abortIfGone())
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sendCtrlKey('V');
    WinInput::sleepMs(300);
    if (abortIfGone())
        return false;

    const int minWait = qMax(1000, static_cast<int>(settings.pasteWaitSeconds * 1000));
    if (!UiaComposer::waitUntilUploaded(hwnd, site, minWait, 120000)) {
        if (abortIfGone())
            return false;
        // 上传等待失败：再查是否套餐限制
        if (UiaComposer::probeWindow(hwnd).imageBlocked) {
            emitNotify(i18n.tr(QStringLiteral("notify_image_blocked"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }
    }
    if (abortIfGone())
        return false;
    if (UiaComposer::probeWindow(hwnd).imageBlocked) {
        emitNotify(i18n.tr(QStringLiteral("notify_image_blocked"),
                           {{QStringLiteral("site"), siteLabel}}));
        return false;
    }

    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (abortIfGone())
            return false;
        WinInput::setForeground(hwnd);
        // 仅首次失败后才再静默聚焦，避免每次回车都点输入框
        if (attempt > 1)
            UiaComposer::focusComposer(hwnd, site);
        WinInput::sleepMs(120);
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
    Q_UNUSED(image);
    return false;
#endif
}
