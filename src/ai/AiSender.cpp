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
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

/**
 * 在 GUI 线程执行 fn 并等待完成，但最多等 timeoutMs。
 * 不用 BlockingQueuedConnection：程序退出或 GUI 线程卡住时它会无限期挂死工作线程，
 * 进而让 QThreadPool 在析构时等不到任务结束。超时后标记过期，迟到的调用会被跳过；
 * Gate 由 shared_ptr 保活，迟到的 lambda 不会访问已销毁的栈变量。
 */
bool runOnGuiThread(const std::function<void()>& fn, int timeoutMs = 5000) {
    if (!qApp || !fn)
        return false;
    if (QThread::currentThread() == qApp->thread()) {
        fn();
        return true;
    }
    struct Gate {
        QSemaphore sem{0};
        std::atomic_bool expired{false};
    };
    const auto gate = std::make_shared<Gate>();
    const bool posted = QMetaObject::invokeMethod(
        qApp,
        [fn, gate]() {
            if (gate->expired.load(std::memory_order_acquire))
                return;
            fn();
            gate->sem.release();
        },
        Qt::QueuedConnection);
    if (!posted)
        return false;
    if (gate->sem.tryAcquire(1, timeoutMs))
        return true;
    gate->expired.store(true, std::memory_order_release);
    return false;
}

// 贴图/上传等待时长（固定值，不做成设置项）：等的是「确认贴上」的最长窗口，
// 实际一确认就立即继续，填大只影响失败时的重试间隔，不拖慢正常发送
constexpr int kSimplePasteWaitMs = 3000; // 简单粘贴站贴图确认窗口
constexpr int kPasteWaitMs = 9000;       // 通用站贴图确认窗口（Gemini 全树扫描慢）
constexpr int kUploadMinWaitMs = 1000;   // 确认上传完成的最短观察时间

} // namespace

AiSender::AiSender(QObject* parent) : QObject(parent) {}

void AiSender::emitNotify(const QString& msg) {
    QMetaObject::invokeMethod(this, [this, msg]() { emit notify(msg); }, Qt::QueuedConnection);
}

void AiSender::sendAsync(const Settings& settings, const QPixmap& image) {
    // 已有发送在进行时直接忽略新请求，避免两个任务同时抢剪贴板与模拟输入
    bool expected = false;
    if (!m_sending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        qWarning("AiSender: send already in progress, ignored");
        return;
    }

    ClipboardImage::copyForPaste(image);
    ClipboardImage::setGuardSuspended(true);

    const QPointer<AiSender> self(this);
    // 把截图带进工作线程：导航若误清剪贴板，发送前可恢复
    QThreadPool::globalInstance()->start([self, settings, image]() {
        // 无论成功失败/对象销毁，先释放占用标记再通知，避免 finished 丢失时永久卡死
        struct SendingGuard {
            QPointer<AiSender> self;
            ~SendingGuard() {
                if (self)
                    self->m_sending.store(false, std::memory_order_release);
                QMetaObject::invokeMethod(
                    qApp, []() { ClipboardImage::setGuardSuspended(false); }, Qt::QueuedConnection);
            }
        };
        const SendingGuard guard{self};

        if (!self)
            return;
        const bool ok = self->sendSync(settings, image);
        if (!self)
            return;
        // 无论成功/拦截/中断：结束时统一把截图以完整格式留在剪贴板
        // （PNG/DIB 能粘图、CF_HDROP 能在桌面/文件夹粘出文件）
        if (!runOnGuiThread([image]() {
                ClipboardImage::setGuardSuspended(false);
                ClipboardImage::copyForPaste(image);
            }))
            qWarning("AiSender: final clipboard restore failed");
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, ok]() {
                if (self)
                    emit self->finished(ok);
            },
            Qt::QueuedConnection);
    });
}

bool AiSender::sendSync(const Settings& settings, const QPixmap& image) {
#ifdef Q_OS_WIN
    platform::ComApartment comApt;
    if (!comApt.ok()) {
        qWarning("AiSender: COM apartment init failed");
        return false;
    }

    auto& i18n = I18n::instance();
    const QString site = settings.normalizedSite();
    const QString siteLabel = SiteCatalog::siteLabel(site);

    // 导航可能误清剪贴板：贴图前在 GUI 线程恢复截图（限时等待，GUI 无响应则放弃发送）
    auto restoreImage = [&image](bool web) {
        return runOnGuiThread([image, web]() {
            if (web)
                ClipboardImage::copyForWebPaste(image);
            else
                ClipboardImage::copyForPaste(image);
        });
    };

    // 启用桌面客户端：只走客户端，绝不回退浏览器
    if (settings.useDesktopClient) {
        if (!WinInput::isDesktopInstalled(site)) {
            emitNotify(i18n.tr(QStringLiteral("notify_desktop_missing"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }

        emitNotify(
            i18n.tr(QStringLiteral("notify_opening"), {{QStringLiteral("label"), siteLabel}}));
        bool launchedDesktop = false;
        HWND hwnd = WinInput::findDesktopWindow(site);
        if (!hwnd) {
            launchedDesktop = true;
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
        // 桌面端已经打开：直接粘贴，不等。只有本次冷启动才等 UIA Document 就绪
        // （后续输入框等待循环也会兜底，这里只是减少冷启动时的无效重试）
        const int warmMs = launchedDesktop ? (SiteCatalog::needsSimplePaste(site) ? 1500 : 600) : 0;
        if (warmMs > 0)
            WinInput::sleepMs(warmMs);
        if (!platform::windowAlive(hwnd)) {
            hwnd = WinInput::findDesktopWindow(site);
            if (!platform::windowAlive(hwnd)) {
                emitNotify(i18n.tr(QStringLiteral("notify_desktop_stopped"),
                                   {{QStringLiteral("site"), siteLabel}}));
                return false;
            }
        }

        if (!restoreImage(false))
            return false;
        if (pastePromptAndSend(settings, reinterpret_cast<quintptr>(hwnd), true, image)) {
            emitNotify(
                i18n.tr(QStringLiteral("notify_sent"), {{QStringLiteral("site"), siteLabel}}));
            return true;
        }
        // 用户删图导致的静默中止：不补任何提示
        if (!m_imageRemovedAbort)
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
        emitNotify(
            i18n.tr(QStringLiteral("notify_open_failed"), {{QStringLiteral("label"), browser}}));
        return false;
    }

    WinInput::setForeground(hwnd);
    // 标签已经打开：直接粘贴，不等。只有本次新开标签/冷启动才给页面起步时间
    // （后面有最长 30s 的输入框就绪等待兜底，页面慢也不会贴错）
    const int pageWait = launched ? (SiteCatalog::needsSimplePaste(site) ? 2500 : 1500) : 0;
    if (pageWait > 0)
        WinInput::sleepMs(pageWait);
    if (!platform::windowAlive(hwnd)) {
        hwnd = WinInput::findBrowserWindow(browser);
        if (!platform::windowAlive(hwnd)) {
            emitNotify(
                i18n.tr(QStringLiteral("notify_stopped"), {{QStringLiteral("browser"), browser}}));
            return false;
        }
    }

    if (!restoreImage(true))
        return false;
    if (pastePromptAndSend(settings, reinterpret_cast<quintptr>(hwnd), false, image)) {
        emitNotify(i18n.tr(QStringLiteral("notify_sent"), {{QStringLiteral("site"), siteLabel}}));
        return true;
    }
    // 用户删图导致的静默中止：不补任何提示
    if (!m_imageRemovedAbort)
        emitNotify(
            i18n.tr(QStringLiteral("notify_stopped"), {{QStringLiteral("browser"), browser}}));
    return false;
#else
    Q_UNUSED(settings);
    Q_UNUSED(image);
    return false;
#endif
}

bool AiSender::pastePromptAndSend(const Settings& settings,
                                  quintptr hwndValue,
                                  bool desktopMode,
                                  const QPixmap& image) {
#ifdef Q_OS_WIN
    // NOLINTNEXTLINE(performance-no-int-to-ptr): HWND 在本类接口中以 quintptr 传递，此处还原
    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    m_imageRemovedAbort = false;
    if (!platform::windowAlive(hwnd))
        return false;

    const QString site = settings.normalizedSite();
    const QString browser = settings.normalizedBrowser();

    auto refreshAlive = [&]() -> bool {
        if (platform::windowAlive(hwnd))
            return true;
        hwnd =
            desktopMode ? WinInput::findDesktopWindow(site) : WinInput::findBrowserWindow(browser);
        return platform::windowAlive(hwnd);
    };

    auto abortIfGone = [&]() -> bool {
        if (refreshAlive())
            return false;
        qWarning("AiSender abort: target window closed");
        return true;
    };

    auto restoreClipImage = [&]() {
        return runOnGuiThread([image, desktopMode]() {
            if (desktopMode)
                ClipboardImage::copyForPaste(image);
            else
                ClipboardImage::copyForWebPaste(image);
        });
    };

    // 给用户保留的最终剪贴板：始终用完整格式（PNG/DIB + CF_HDROP），
    // 这样既能粘图片到应用里，也能在桌面/文件夹里直接粘出 PNG 文件。
    // 注意与 restoreClipImage 区分：贴图进网页前必须用不带 CF_HDROP 的网页格式，
    // 否则豆包等网页会走文件路径而非图片上传。
    auto restoreClipImageFull = [&]() {
        return runOnGuiThread([image]() { ClipboardImage::copyForPaste(image); });
    };

    UiaComposer::enableWebAccessibility(hwnd);
    WinInput::setForeground(hwnd);

    // 若误进技能/生图等非聊天页，用浏览器直接打开目标聊天 URL。
    // 只有配了 avoidUrlHints 的站才需要读地址栏（一次全树扫描不便宜），其余站跳过
    if (!desktopMode && !SiteCatalog::avoidUrlHints(site).isEmpty()) {
        const QString cur = BrowserNav::readAddressBarUrl(hwnd).toLower();
        bool bad = false;
        for (const QString& h : SiteCatalog::avoidUrlHints(site)) {
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
            if (!restoreClipImage())
                return false;
            WinInput::sleepMs(800);
        }
    }

    QString prompt = settings.promptText.trimmed();
    if (prompt.isEmpty())
        prompt = I18n::defaultPrompt(settings.normalizedLanguage());

    auto& i18n = I18n::instance();
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

    // 已上传图片被用户手动删除的复核：连续两次扫描附件数低于贴图后数量才判定，
    // 避免单次 UIA 扫描抖动误杀正常发送
    auto attachmentRemovedNow = [&](int expected) {
        if (expected < 0)
            return false;
        if (UiaComposer::scan(hwnd, site).attachments >= expected)
            return false;
        WinInput::sleepMs(300);
        return platform::windowAlive(hwnd) && UiaComposer::scan(hwnd, site).attachments < expected;
    };
    auto markImageRemoved = [&]() {
        // 用户主动删图属于正常操作：静默中止，不弹任何托盘提示
        m_imageRemovedAbort = true;
        qDebug() << "AiSender: uploaded image removed by user, abort silently";
    };

    // 等输入框真正出现（新开标签页/冷启动尤其需要）：必须 UIA 真找到输入框控件
    // 才算就绪。focusComposer 找不到时会坐标盲点且恒返回成功，页面没渲染完时
    // 若只看它就会提前放行，Ctrl+V 贴空（豆包客户端冷启动实测踩过）
    {
        const ULONGLONG deadline = GetTickCount64() + 30000;
        while (GetTickCount64() < deadline) {
            if (abortIfGone())
                return false;
            if (UiaComposer::composerPresent(hwnd, site) && UiaComposer::focusComposer(hwnd, site))
                break;
            WinInput::sleepMs(400);
        }
    }

    // 等页面附件计数稳定后才取贴图基线：冷启动/刚打开页面时首页图片还在陆续
    // 加载，计数自己会涨，若直接贴图会被误判成「图片已贴上」而只发文字。
    // 稳定 = 输入区与全窗计数连续两次相同；已稳定的页面只多等一个轮询间隔
    auto waitCountsStable = [&](bool includeProbe, int& scanAtt, int& winAtt) {
        scanAtt = UiaComposer::scan(hwnd, site).attachments;
        winAtt = includeProbe ? UiaComposer::probeWindow(hwnd).attachments : -1;
        const ULONGLONG until = GetTickCount64() + 6000;
        while (GetTickCount64() < until) {
            if (!platform::windowAlive(hwnd))
                return;
            WinInput::sleepMs(250);
            const int s = UiaComposer::scan(hwnd, site).attachments;
            const int w = includeProbe ? UiaComposer::probeWindow(hwnd).attachments : winAtt;
            if (s == scanAtt && w == winAtt)
                return;
            scanAtt = s;
            winAtt = w;
        }
    };

    // 豆包/千问/DeepSeek：只点聊天输入框（绝不点「图像生成」等技能），再 Ctrl+V 上传
    if (SiteCatalog::needsSimplePaste(site)) {
        if (abortIfGone())
            return false;

        UiaComposer::ImagePasteResult pasteRc = UiaComposer::ImagePasteResult::Failed;
        int lastBeforeAtt = 0;
        int lastScanBeforeAtt = 0;
        int stableWinAtt = -1;
        for (int attempt = 0; attempt < 3 && pasteRc != UiaComposer::ImagePasteResult::Ok &&
                              pasteRc != UiaComposer::ImagePasteResult::Blocked;
             ++attempt) {
            if (abortIfGone())
                return false;
            if (!restoreClipImage())
                return false;
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
            WinInput::sleepMs(100);
            if (abortIfGone())
                return false;
            // 贴图前先等页面计数稳定（冷启动加载中会自己涨，不能当贴图基线）；
            // 只第一次做完整稳定等待，重试时页面早已稳定，只刷新输入区基线
            if (attempt == 0) {
                waitCountsStable(true, lastScanBeforeAtt, lastBeforeAtt);
                stableWinAtt = lastBeforeAtt;
            } else {
                lastScanBeforeAtt = UiaComposer::scan(hwnd, site).attachments;
                lastBeforeAtt = stableWinAtt;
            }
            if (abortIfGone())
                return false;
            WinInput::sendCtrlKey('V');
            pasteRc = UiaComposer::waitImagePasteResult(
                hwnd, site, lastScanBeforeAtt, kSimplePasteWaitMs, lastBeforeAtt);
            qDebug() << "AiSender simplePaste attempt" << attempt << "rc" << int(pasteRc);
            if (pasteRc == UiaComposer::ImagePasteResult::WindowGone)
                return false;
        }
        if (abortIfGone())
            return false;
        // Failed / Blocked：不贴文字、不回车
        if (!stopAfterImageIssue(pasteRc))
            return false;

        // 贴上后硬复核（任一不过，后续贴字/发送绝不执行）：
        // 1) 网站若弹了限制提示（未登录/套餐/不支持图片），立即停
        const UiaComposer::State probeAfter = UiaComposer::probeWindow(hwnd);
        if (probeAfter.imageBlocked) {
            emitNotify(i18n.tr(QStringLiteral("notify_image_blocked"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }
        // 2) 输入区附件必须真的比贴图前多：没多说明图没进输入区——全窗也没多
        //    是被删/从未贴上（静默停）；全窗多了但输入区没多，多半是限制弹窗
        //    里的图片在凑数，同样按贴图失败停，绝不只发文字
        const int pastedAtt = UiaComposer::scan(hwnd, site).attachments;
        if (pastedAtt <= lastScanBeforeAtt) {
            if (probeAfter.attachments <= lastBeforeAtt)
                markImageRemoved();
            else
                emitNotify(i18n.tr(QStringLiteral("notify_image_failed"),
                                   {{QStringLiteral("site"), siteLabel}}));
            return false;
        }

        // 贴图后焦点仍在输入框：直接贴字/回车，不再二次点击（避免指针闪烁）
        if (!runOnGuiThread([prompt]() { QApplication::clipboard()->setText(prompt); }))
            return false;
        WinInput::sleepMs(80);
        if (abortIfGone())
            return false;
        WinInput::setForeground(hwnd);
        WinInput::sendCtrlKey('V');
        WinInput::sleepMs(220);
        // 不在这里恢复剪贴板（要重新编码整图、耗时）：sendAsync 结束时统一恢复，
        // 贴字后直接回车，少一次往返延迟
        if (abortIfGone())
            return false;
        // 回车前最后一道闸：图片已被用户删除则不发送
        if (attachmentRemovedNow(pastedAtt)) {
            markImageRemoved();
            return false;
        }
        // 回车前限制复查：贴完字后才弹出的限制提示也要拦住，绝不带字发送
        if (UiaComposer::probeWindow(hwnd).imageBlocked) {
            emitNotify(i18n.tr(QStringLiteral("notify_image_blocked"),
                               {{QStringLiteral("site"), siteLabel}}));
            return false;
        }
        WinInput::sendEnter();
        WinInput::sleepMs(800);
        return platform::windowAlive(hwnd);
    }
    if (abortIfGone())
        return false;

    UiaComposer::clearComposer(hwnd, site);
    if (abortIfGone())
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sleepMs(100);

    UiaComposer::ImagePasteResult pasteRc = UiaComposer::ImagePasteResult::Failed;
    int lastBeforeAtt = 0;
    int lastScanBeforeAtt = 0;
    int firstBeforeAtt = -1;
    int firstWinBeforeAtt = -1;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (abortIfGone())
            return false;
        if (!restoreClipImage())
            return false;
        // 上一轮贴图可能只是检测超时、图片其实已迟到附上：先确认，避免重复贴图。
        // 只认输入区计数（全窗会把弹窗/历史图片算进来，不能作为贴上依据）
        if (attempt > 1 && firstBeforeAtt >= 0 &&
            UiaComposer::scan(hwnd, site).attachments > firstBeforeAtt) {
            pasteRc = UiaComposer::ImagePasteResult::Ok;
            break;
        }
        int scanBefore = UiaComposer::scan(hwnd, site).attachments;
        // 全窗基线只在第一次贴图前取（每轮都取太贵）：先等计数稳定再取，
        // 否则页面加载中图片自己变多会被误判成贴图成功
        if (attempt == 1) {
            waitCountsStable(true, scanBefore, firstWinBeforeAtt);
            lastBeforeAtt = firstWinBeforeAtt;
            if (abortIfGone())
                return false;
        }
        lastScanBeforeAtt = scanBefore;
        if (firstBeforeAtt < 0)
            firstBeforeAtt = scanBefore;
        if (!UiaComposer::focusComposer(hwnd, site)) {
            RECT r{};
            if (GetWindowRect(hwnd, &r) && r.right > r.left && r.bottom > r.top) {
                const int cx = r.left + static_cast<int>((r.right - r.left) * 0.50);
                const int cy = r.top + static_cast<int>((r.bottom - r.top) * 0.92);
                WinInput::clickScreen(cx, cy, false);
            }
        }
        WinInput::sleepMs(100);
        if (abortIfGone())
            return false;
        WinInput::setForeground(hwnd);
        WinInput::sendCtrlKey('V');
        // Gemini 等重页面一次 UIA 全树扫描就要数秒，等待窗口必须留够扫描+上传的时间
        pasteRc = UiaComposer::waitImagePasteResult(
            hwnd, site, scanBefore, kPasteWaitMs, firstWinBeforeAtt);
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

    // 贴上后硬复核：输入区附件必须真的比贴图前多，否则后续绝不执行。
    // 全窗也没多 = 刚贴上就被删/从未贴上（静默停）；全窗多了但输入区没多，
    // 多半是限制弹窗里的图片在凑数——按贴图失败停，绝不只发文字
    const int pastedAtt = UiaComposer::scan(hwnd, site).attachments;
    if (pastedAtt <= lastScanBeforeAtt) {
        if (UiaComposer::probeWindow(hwnd).attachments <= lastBeforeAtt)
            markImageRemoved();
        else
            emitNotify(i18n.tr(QStringLiteral("notify_image_failed"),
                               {{QStringLiteral("site"), siteLabel}}));
        return false;
    }

    // 先确认图片真正上传完成（发送可用且非上传中），之后贴字立即回车发送，
    // 不再在文字贴完后重复等一轮上传检测
    bool removedByUser = false;
    if (!UiaComposer::waitUntilUploaded(
            hwnd, site, kUploadMinWaitMs, 120000, pastedAtt, &removedByUser)) {
        if (removedByUser) {
            markImageRemoved();
            return false;
        }
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
        emitNotify(
            i18n.tr(QStringLiteral("notify_image_blocked"), {{QStringLiteral("site"), siteLabel}}));
        return false;
    }

    // 贴图后不再点输入框，直接贴提示词（指针不闪）
    if (!runOnGuiThread([prompt]() { QApplication::clipboard()->setText(prompt); }))
        return false;
    WinInput::sleepMs(80);
    if (abortIfGone())
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sendCtrlKey('V');
    WinInput::sleepMs(200);
    // 不在这里恢复剪贴板（要重新编码整图、耗时）：sendAsync 结束时统一恢复
    if (abortIfGone())
        return false;
    // 回车前最后一道闸：图片已被用户删除则不发送（文字虽已贴入，也绝不回车）
    if (attachmentRemovedNow(pastedAtt)) {
        markImageRemoved();
        return false;
    }
    // 回车前限制复查：上传阶段才弹出的限制提示也要拦住，绝不带字发送
    if (UiaComposer::probeWindow(hwnd).imageBlocked) {
        emitNotify(
            i18n.tr(QStringLiteral("notify_image_blocked"), {{QStringLiteral("site"), siteLabel}}));
        return false;
    }

    // 图片已确认上传完成、文字已贴入：立即回车发送，不再等待
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (abortIfGone())
            return false;
        WinInput::setForeground(hwnd);
        // 仅首次失败后才再静默聚焦，避免每次回车都点输入框
        if (attempt > 1)
            UiaComposer::focusComposer(hwnd, site);
        WinInput::sleepMs(60);
        if (abortIfGone())
            return false;
        WinInput::sendEnter();
        WinInput::sleepMs(1000);
        if (abortIfGone())
            return false;
        if (UiaComposer::messageSent(hwnd, site, prompt))
            return true;
        // 上传在贴字前已确认完成，重试只需重新回车，不再重复等上传
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
