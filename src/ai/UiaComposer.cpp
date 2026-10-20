#include "ai/UiaComposer.h"

#include "ai/SiteCatalog.h"
#include "ai/WinInput.h"
#include "platform/UiaCore.h"

#include <QDebug>
#include <QList>
#include <QStringList>

#include <algorithm>
#include <functional>

#ifdef Q_OS_WIN
#include <oleacc.h>
#endif

namespace UiaComposer {

#ifdef Q_OS_WIN

namespace {

const QStringList kUploadingHints = {QStringLiteral("上传中"),
                                     QStringLiteral("正在上传"),
                                     QStringLiteral("正在加载"),
                                     QStringLiteral("处理中"),
                                     QStringLiteral("uploading"),
                                     QStringLiteral("loading"),
                                     QStringLiteral("processing")};
const QStringList kSendHints = {QStringLiteral("发送"), QStringLiteral("send")};
const QStringList kRemoveHints = {QStringLiteral("移除"),
                                  QStringLiteral("删除"),
                                  QStringLiteral("remove"),
                                  QStringLiteral("去掉"),
                                  QStringLiteral("clear file")};
/** 明确与「图片/文件上传失败或套餐限制」相关的文案（避免侧栏「升级」误报） */
/** 豆包技能条等：绝不能点（会进「图像生成」等模式，不是上传附件） */
const QStringList kSkillClickAvoid = {QStringLiteral("图像生成"),
                                      QStringLiteral("图片生成"),
                                      QStringLiteral("生成图片"),
                                      QStringLiteral("视频生成"),
                                      QStringLiteral("生成视频"),
                                      QStringLiteral("ppt"),
                                      QStringLiteral("帮我写作"),
                                      QStringLiteral("录音转写"),
                                      QStringLiteral("音乐生成"),
                                      QStringLiteral("create-image"),
                                      QStringLiteral("image generation"),
                                      QStringLiteral("skill")};

const QStringList kImageBlockHints = {
    // 中文 — 须直接相关上传/图片
    QStringLiteral("不支持图片"),
    QStringLiteral("不支持上传图片"),
    QStringLiteral("无法上传图片"),
    QStringLiteral("不能上传图片"),
    QStringLiteral("暂不支持图片"),
    QStringLiteral("暂不支持上传图片"),
    QStringLiteral("图片上传失败"),
    QStringLiteral("上传图片失败"),
    QStringLiteral("图片发送失败"),
    QStringLiteral("免费版不支持"),
    QStringLiteral("免费用户无法"),
    QStringLiteral("免费计划不支持"),
    QStringLiteral("升级后才能上传"),
    QStringLiteral("升级后可上传"),
    QStringLiteral("会员才能上传"),
    QStringLiteral("文件过大"),
    QStringLiteral("图片过大"),
    QStringLiteral("格式不支持"),
    QStringLiteral("不支持该文件"),
    QStringLiteral("不支持此文件"),
    QStringLiteral("上传失败"),
    // 未登录 / 登录墙（网页常见：可打字但不可上传）
    QStringLiteral("请先登录"),
    QStringLiteral("请登录"),
    QStringLiteral("登录后"),
    QStringLiteral("登录即可"),
    QStringLiteral("立即登录"),
    QStringLiteral("未登录"),
    QStringLiteral("登录账号"),
    QStringLiteral("扫码登录"),
    QStringLiteral("登录后继续"),
    QStringLiteral("登录后使用"),
    QStringLiteral("登录后畅享"),
    // English
    QStringLiteral("can't upload"),
    QStringLiteral("cannot upload"),
    QStringLiteral("unable to upload"),
    QStringLiteral("upload failed"),
    QStringLiteral("failed to upload"),
    QStringLiteral("image uploads are not"),
    QStringLiteral("file uploads are not"),
    QStringLiteral("uploads are not available"),
    QStringLiteral("not available on the free"),
    QStringLiteral("plus to upload"),
    QStringLiteral("pro to upload"),
    QStringLiteral("upgrade to upload"),
    QStringLiteral("unsupported file"),
    QStringLiteral("file too large"),
    QStringLiteral("image too large"),
    QStringLiteral("vision is only"),
    QStringLiteral("only plus can"),
    QStringLiteral("only pro can"),
    QStringLiteral("sign in to"),
    QStringLiteral("log in to"),
    QStringLiteral("please log in"),
    QStringLiteral("please sign in"),
    QStringLiteral("login required"),
    QStringLiteral("sign in required")};

QString norm(const QString& s) {
    return s.trimmed().toLower();
}

bool containsAny(const QString& text, const QStringList& hints) {
    for (const QString& h : hints) {
        if (text.contains(h))
            return true;
    }
    return false;
}

bool looksLikeImageBlock(const QString& name) {
    if (name.isEmpty() || name.size() > 200)
        return false;
    if (!containsAny(name, kImageBlockHints))
        return false;
    // 「上传失败」等稍宽的词：需同时出现图/文件/image/file/upload 语义
    if (name.contains(QLatin1String("上传失败")) || name.contains(QLatin1String("upload failed")) ||
        name.contains(QLatin1String("failed to upload"))) {
        return name.contains(QLatin1String("图")) || name.contains(QLatin1String("image")) ||
               name.contains(QLatin1String("file")) || name.contains(QLatin1String("文件")) ||
               name.contains(QLatin1String("png")) || name.contains(QLatin1String("jpg")) ||
               name.contains(QLatin1String("附件")) || name.contains(QLatin1String("attach"));
    }
    return true;
}

RECT windowRect(HWND hwnd) {
    RECT r{};
    GetWindowRect(hwnd, &r);
    return r;
}

IUIAutomationTreeWalker* walker() {
    return platform::UiaCore::instance().controlWalker();
}

struct ComposerHit {
    IUIAutomationElement* el = nullptr;
    int matched = 1; // 0=matched hint
    LONG bottom = 0;
    bool isDocument = false; // Chromium 桌面端（豆包等）常见 Document 而非 Edit
};

void walkFindComposer(IUIAutomationElement* el,
                      const RECT& win,
                      LONG contentTop,
                      const QStringList& hints,
                      int depth,
                      QList<ComposerHit>& hits) {
    if (!el || depth > 70)
        return;

    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    const bool isEdit = ctype == UIA_EditControlTypeId;
    // 豆包 / 千问等 Electron·Chromium 壳往往只暴露 Document
    const bool isDoc = ctype == UIA_DocumentControlTypeId;
    if (isEdit || isDoc) {
        BSTR clsB = nullptr;
        BSTR nameB = nullptr;
        el->get_CurrentClassName(&clsB);
        el->get_CurrentName(&nameB);
        RECT br{};
        el->get_CurrentBoundingRectangle(&br);
        const QString cls = clsB ? QString::fromWCharArray(clsB) : QString();
        const QString name = nameB ? QString::fromWCharArray(nameB) : QString();
        if (clsB)
            SysFreeString(clsB);
        if (nameB)
            SysFreeString(nameB);

        const LONG w = br.right - br.left;
        const LONG h = br.bottom - br.top;
        const bool visible = w > 20 && h > 8;
        const bool notOmni =
            cls != QLatin1String("OmniboxViewViews") && cls != QLatin1String("urlbar-input");
        // Document 常覆盖大半窗口：要求贴底且足够宽，避免点到侧栏
        const LONG winH = win.bottom - win.top;
        const LONG winW = win.right - win.left;
        const bool docOk =
            !isDoc || (w >= winW * 0.35 && br.bottom >= win.top + static_cast<LONG>(winH * 0.55));
        if (visible && notOmni && docOk && br.top > contentTop - (isDoc ? 80 : 0) &&
            br.left >= win.left - 2 && br.right <= win.right + 8) {
            const QString text = norm(name + QLatin1Char(' ') + cls);
            // 跳过技能入口文案（「图像生成」等），避免当成输入框
            if (!containsAny(text, kSkillClickAvoid)) {
                ComposerHit hit;
                hit.el = el;
                hit.el->AddRef();
                // Edit 优先于 Document；有 hint 再优先
                hit.matched = (isDoc ? 2 : 0) + (containsAny(text, hints) ? 0 : 1);
                hit.bottom = br.bottom;
                hit.isDocument = isDoc;
                hits.push_back(hit);
            }
        }
    }

    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return;
    IUIAutomationElement* child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        walkFindComposer(child, win, contentTop, hints, depth + 1, hits);
        IUIAutomationElement* next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

struct ComposerPick {
    IUIAutomationElement* el = nullptr;
    bool isDocument = false;
};

ComposerPick findComposerPick(HWND hwnd, const QString& site) {
    ComposerPick pick;
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return pick;
    const RECT win = windowRect(hwnd);
    const LONG contentTop = win.top + 80;
    QList<ComposerHit> hits;
    walkFindComposer(root.get(), win, contentTop, SiteCatalog::composerHints(site), 0, hits);
    if (hits.isEmpty())
        return pick;
    std::sort(hits.begin(), hits.end(), [](const ComposerHit& a, const ComposerHit& b) {
        if (a.matched != b.matched)
            return a.matched < b.matched;
        return a.bottom > b.bottom;
    });
    pick.el = hits.first().el;
    pick.isDocument = hits.first().isDocument;
    for (int i = 1; i < hits.size(); ++i)
        hits[i].el->Release();
    return pick;
}

IUIAutomationElement* findComposerCtrl(HWND hwnd, const QString& site) {
    return findComposerPick(hwnd, site).el;
}

IUIAutomationElement* composerContainer(IUIAutomationElement* composer, HWND hwnd) {
    if (!composer)
        return nullptr;
    const RECT win = windowRect(hwnd);
    const LONG maxH = static_cast<LONG>((win.bottom - win.top) * 0.45);
    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return nullptr;

    IUIAutomationElement* container = composer;
    container->AddRef();
    for (int i = 0; i < 12; ++i) {
        IUIAutomationElement* parent = nullptr;
        w->GetParentElement(container, &parent);
        if (!parent)
            break;
        RECT br{};
        parent->get_CurrentBoundingRectangle(&br);
        if (br.bottom - br.top > maxH) {
            parent->Release();
            break;
        }
        container->Release();
        container = parent;
    }
    return container;
}

void scanTree(IUIAutomationElement* el,
              int depth,
              State& state,
              LONG& sendBottom,
              int& removeButtons,
              int& thumbnails,
              int maxDepth,
              bool detectBlocks) {
    if (!el || depth > maxDepth)
        return;

    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    RECT br{};
    el->get_CurrentBoundingRectangle(&br);
    const bool inRegion = (br.right - br.left > 0) && (br.bottom - br.top > 0);
    if (inRegion) {
        BSTR nameB = nullptr;
        BSTR helpB = nullptr;
        el->get_CurrentName(&nameB);
        el->get_CurrentHelpText(&helpB);
        QString name =
            norm((nameB ? QString::fromWCharArray(nameB) : QString()) + QLatin1Char(' ') +
                 (helpB ? QString::fromWCharArray(helpB) : QString()));
        if (nameB)
            SysFreeString(nameB);
        if (helpB)
            SysFreeString(helpB);

        BOOL enabled = TRUE;
        el->get_CurrentIsEnabled(&enabled);

        if (detectBlocks && looksLikeImageBlock(name))
            state.imageBlocked = true;

        if (ctype == UIA_ProgressBarControlTypeId ||
            (name.size() <= 40 && containsAny(name, kUploadingHints))) {
            state.uploading = true;
        }
        if (ctype == UIA_ButtonControlTypeId && containsAny(name, kRemoveHints))
            removeButtons++;
        if (ctype == UIA_ImageControlTypeId && (br.right - br.left) >= 24 &&
            (br.bottom - br.top) >= 24)
            thumbnails++;
        if (ctype == UIA_ButtonControlTypeId && name.size() <= 40 &&
            containsAny(name, kSendHints) && br.bottom > sendBottom) {
            sendBottom = br.bottom;
            state.sendFound = true;
            state.sendEnabled = enabled;
        }
    }

    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return;
    IUIAutomationElement* child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        scanTree(
            child, depth + 1, state, sendBottom, removeButtons, thumbnails, maxDepth, detectBlocks);
        IUIAutomationElement* next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

bool trySetFocus(IUIAutomationElement* el) {
    if (!el)
        return false;
    if (SUCCEEDED(el->SetFocus()))
        return true;
    IUIAutomationLegacyIAccessiblePattern* leg = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                          IID_IUIAutomationLegacyIAccessiblePattern,
                                          reinterpret_cast<void**>(&leg))) &&
        leg) {
        // accSelect SELFLAG_TAKEFOCUS = 1
        const HRESULT hr = leg->Select(0x1);
        leg->Release();
        if (SUCCEEDED(hr))
            return true;
    }
    return false;
}

/** 必须点坐标时：静默消息点击，指针不移动 */
void clickPoint(int x, int y) {
    WinInput::clickScreen(x, y, false);
}

QString readEditValue(IUIAutomationElement* el) {
    if (!el)
        return {};
    IUIAutomationValuePattern* pat = nullptr;
    if (FAILED(el->GetCurrentPatternAs(
            UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&pat))) ||
        !pat)
        return {};
    BSTR val = nullptr;
    pat->get_CurrentValue(&val);
    QString s = val ? QString::fromWCharArray(val) : QString();
    if (val)
        SysFreeString(val);
    pat->Release();
    return s;
}

void invokeRemoveButtons(IUIAutomationElement* container) {
    if (!container)
        return;
    // 简化：扫描并 Invoke 移除按钮
    struct Item {
        IUIAutomationElement* el;
    };
    QList<Item> buttons;

    std::function<void(IUIAutomationElement*, int)> walk;
    walk = [&](IUIAutomationElement* el, int depth) {
        if (!el || depth > 30)
            return;
        CONTROLTYPEID ctype = 0;
        el->get_CurrentControlType(&ctype);
        if (ctype == UIA_ButtonControlTypeId) {
            BSTR nameB = nullptr;
            el->get_CurrentName(&nameB);
            const QString name = nameB ? norm(QString::fromWCharArray(nameB)) : QString();
            if (nameB)
                SysFreeString(nameB);
            if (containsAny(name, kRemoveHints)) {
                el->AddRef();
                buttons.push_back({el});
            }
        }
        IUIAutomationTreeWalker* w = walker();
        if (!w)
            return;
        IUIAutomationElement* child = nullptr;
        w->GetFirstChildElement(el, &child);
        while (child) {
            walk(child, depth + 1);
            IUIAutomationElement* next = nullptr;
            w->GetNextSiblingElement(child, &next);
            child->Release();
            child = next;
        }
    };
    walk(container, 0);

    for (Item& it : buttons) {
        IUIAutomationInvokePattern* inv = nullptr;
        if (SUCCEEDED(it.el->GetCurrentPatternAs(UIA_InvokePatternId,
                                                 IID_IUIAutomationInvokePattern,
                                                 reinterpret_cast<void**>(&inv))) &&
            inv) {
            inv->Invoke();
            inv->Release();
        }
        it.el->Release();
        WinInput::sleepMs(200);
    }
}

} // namespace

void enableWebAccessibility(HWND hwnd) {
    if (!platform::windowAlive(hwnd))
        return;
    // 短缓存：同一窗口 2s 内不重复 EnumChild + AccessibleObjectFromWindow
    static HWND s_lastHwnd = nullptr;
    static ULONGLONG s_lastTick = 0;
    const ULONGLONG now = GetTickCount64();
    if (hwnd == s_lastHwnd && (now - s_lastTick) < 2000ULL)
        return;
    s_lastHwnd = hwnd;
    s_lastTick = now;

    QList<HWND> targets;
    EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM lp) -> BOOL {
            // Win32 EnumWindows 回调经 LPARAM 传容器指针，为系统约定用法
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto* list = reinterpret_cast<QList<HWND>*>(lp);
            wchar_t cls[128]{};
            GetClassNameW(child, cls, 128);
            const QString c = QString::fromWCharArray(cls);
            if (c == QLatin1String("Chrome_RenderWidgetHostHWND") ||
                c == QLatin1String("MozillaWindowClass"))
                list->push_back(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&targets));
    targets.push_back(hwnd);

    const IID iidAcc = IID_IAccessible;
    for (HWND t : targets) {
        IAccessible* acc = nullptr;
        if (AccessibleObjectFromWindow(
                t, static_cast<DWORD>(OBJID_CLIENT), iidAcc, reinterpret_cast<void**>(&acc)) ==
                S_OK &&
            acc) {
            acc->Release();
        }
    }
}

bool focusComposer(HWND hwnd, const QString& site) {
    enableWebAccessibility(hwnd);
    WinInput::setForeground(hwnd);

    const QString s = SiteCatalog::normalizeSite(site);
    const bool simpleShell = SiteCatalog::needsSimplePaste(s);

    // 豆包/千问/DeepSeek：技能条在输入框上方；只点更靠下的文本框中央（全站同一策略）
    auto clickClientComposer = [&]() -> bool {
        RECT rc{};
        if (!GetClientRect(hwnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top)
            return false;
        POINT pt{};
        if (simpleShell) {
            // 主栏输入框中下部，避开上方技能 chip、左侧「+」、右侧发送
            pt.x = rc.left + static_cast<LONG>((rc.right - rc.left) * 0.52);
            pt.y = rc.top + static_cast<LONG>((rc.bottom - rc.top) * 0.965);
        } else {
            pt.x = rc.left + static_cast<LONG>((rc.right - rc.left) * 0.55);
            pt.y = rc.top + static_cast<LONG>((rc.bottom - rc.top) * 0.93);
        }
        if (!ClientToScreen(hwnd, &pt))
            return false;
        clickPoint(static_cast<int>(pt.x), static_cast<int>(pt.y));
        WinInput::sleepMs(220);
        return true;
    };

    // 优先纯 SetFocus（零指针移动）；失败再用静默消息点击文本区
    ComposerPick pick = findComposerPick(hwnd, site);
    if (pick.el) {
        const bool focused = trySetFocus(pick.el);
        RECT br{};
        const HRESULT hr = pick.el->get_CurrentBoundingRectangle(&br);
        const bool isDoc = pick.isDocument;
        pick.el->Release();
        if (focused && !simpleShell) {
            // ChatGPT/Claude/Gemini：SetFocus 通常足够，避免再点
            WinInput::sleepMs(120);
            return true;
        }
        if (SUCCEEDED(hr) && br.right > br.left && br.bottom > br.top) {
            const LONG winH = windowRect(hwnd).bottom - windowRect(hwnd).top;
            const LONG h = br.bottom - br.top;
            const int cx = (br.left + br.right) / 2;
            int cy = (br.top + br.bottom) / 2;
            if (isDoc && h > winH * 0.35)
                cy = br.bottom - 24;
            else if (simpleShell)
                cy = br.bottom - qMax(12L, h / 5);
            clickPoint(cx, qMax(br.top + 8, cy));
            WinInput::sleepMs(160);
            return true;
        }
        if (focused) {
            WinInput::sleepMs(120);
            return true;
        }
    }

    return clickClientComposer();
}

State scan(HWND hwnd, const QString& site) {
    State state;
    enableWebAccessibility(hwnd);
    IUIAutomationElement* composer = findComposerCtrl(hwnd, site);
    if (!composer)
        return state;
    IUIAutomationElement* container = composerContainer(composer, hwnd);
    composer->Release();
    if (!container)
        return state;

    state.composerFound = true;
    LONG sendBottom = -1;
    int removeButtons = 0;
    int thumbnails = 0;
    scanTree(container, 0, state, sendBottom, removeButtons, thumbnails, 30, true);
    state.attachments = qMax(removeButtons, thumbnails);
    container->Release();
    return state;
}

State probeWindow(HWND hwnd) {
    State state;
    enableWebAccessibility(hwnd);
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return state;
    LONG sendBottom = -1;
    int removeButtons = 0;
    int thumbnails = 0;
    // 全窗扫限制文案 + 附件缩略图（深度稍浅，控耗时）
    scanTree(root.get(), 0, state, sendBottom, removeButtons, thumbnails, 24, true);
    state.attachments = qMax(removeButtons, thumbnails);
    state.composerFound = true;
    return state;
}

bool waitAttachmentAdded(HWND hwnd, const QString& site, int before, int timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    while (GetTickCount64() < deadline) {
        if (!platform::windowAlive(hwnd))
            return false;
        WinInput::sleepMs(400);
        if (!platform::windowAlive(hwnd))
            return false;
        const State st = scan(hwnd, site);
        if (st.imageBlocked)
            return false;
        if (st.attachments > before || (st.uploading && before == 0))
            return true;
        // 全窗再确认一次（toast 可能不在 composer 容器内）
        const State win = probeWindow(hwnd);
        if (win.imageBlocked)
            return false;
        if (win.attachments > before)
            return true;
    }
    return false;
}

ImagePasteResult waitImagePasteResult(HWND hwnd,
                                      const QString& site,
                                      int beforeAttachments,
                                      int timeoutMs) {
    Q_UNUSED(site);
    if (!platform::windowAlive(hwnd))
        return ImagePasteResult::WindowGone;

    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(qMax(2000, timeoutMs));
    bool sawUpload = false;
    while (GetTickCount64() < deadline) {
        if (!platform::windowAlive(hwnd))
            return ImagePasteResult::WindowGone;
        WinInput::sleepMs(350);
        if (!platform::windowAlive(hwnd))
            return ImagePasteResult::WindowGone;

        State st = scan(hwnd, site);
        State win = probeWindow(hwnd);
        if (st.imageBlocked || win.imageBlocked)
            return ImagePasteResult::Blocked;
        if (st.uploading || win.uploading)
            sawUpload = true;
        const int att = qMax(st.attachments, win.attachments);
        if (att > beforeAttachments)
            return ImagePasteResult::Ok;
        // 出现过上传中且已结束、附件仍未增加：可能被拒
        if (sawUpload && !st.uploading && !win.uploading && GetTickCount64() + 800 >= deadline) {
            break;
        }
    }

    // 超时前最后扫一次限制文案
    if (!platform::windowAlive(hwnd))
        return ImagePasteResult::WindowGone;
    const State last = probeWindow(hwnd);
    if (last.imageBlocked)
        return ImagePasteResult::Blocked;
    if (last.attachments > beforeAttachments)
        return ImagePasteResult::Ok;
    return ImagePasteResult::Failed;
}

bool waitUntilUploaded(HWND hwnd, const QString& site, int minWaitMs, int maxWaitMs) {
    qDebug() << "UIA wait upload min=" << minWaitMs << "max=" << maxWaitMs;
    if (!platform::windowAlive(hwnd))
        return false;
    const ULONGLONG start = GetTickCount64();
    WinInput::sleepMs(800);
    if (!platform::windowAlive(hwnd))
        return false;
    const ULONGLONG deadline = start + static_cast<ULONGLONG>(qMax(10000, maxWaitMs));
    int readyStreak = 0;

    while (GetTickCount64() < deadline) {
        if (!platform::windowAlive(hwnd)) {
            qDebug() << "UIA upload wait aborted: window closed";
            return false;
        }
        const State st = scan(hwnd, site);
        if (st.imageBlocked || probeWindow(hwnd).imageBlocked)
            return false;
        const int elapsed = static_cast<int>(GetTickCount64() - start);
        bool ready = false;
        if (st.sendFound)
            ready = st.sendEnabled && !st.uploading;
        else
            ready = !st.uploading && elapsed >= minWaitMs;

        readyStreak = ready ? readyStreak + 1 : 0;
        if (readyStreak >= 2) {
            qDebug() << "UIA upload ready after" << elapsed << "ms"
                     << "attachments=" << st.attachments << "send=" << st.sendEnabled;
            WinInput::sleepMs(300);
            return true;
        }
        // 就绪前稍拉长间隔，降低全树扫描频率
        WinInput::sleepMs(ready ? 450 : 700);
    }
    qDebug() << "UIA upload wait timeout";
    return false;
}

bool messageSent(HWND hwnd, const QString& site, const QString& prompt) {
    enableWebAccessibility(hwnd);
    IUIAutomationElement* ctrl = findComposerCtrl(hwnd, site);
    if (ctrl) {
        const QString text = readEditValue(ctrl);
        ctrl->Release();
        const QString probe = prompt.trimmed().left(6);
        if (!probe.isEmpty() && !text.isNull())
            return !text.contains(probe);
    }
    const State st = scan(hwnd, site);
    // 找不到输入区时不能当作已发送
    if (!st.composerFound)
        return false;
    return !(st.sendFound && st.sendEnabled);
}

void clearComposer(HWND hwnd, const QString& site) {
    enableWebAccessibility(hwnd);
    if (SiteCatalog::needsSimplePaste(site)) {
        focusComposer(hwnd, site);
        return;
    }
    IUIAutomationElement* composer = findComposerCtrl(hwnd, site);
    if (composer) {
        IUIAutomationElement* container = composerContainer(composer, hwnd);
        composer->Release();
        if (container) {
            invokeRemoveButtons(container);
            container->Release();
        }
    }
    focusComposer(hwnd, site);
    WinInput::sendCtrlKey('A');
    WinInput::sleepMs(50);
    // Delete
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_DELETE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    WinInput::sleepMs(200);
}

#endif

} // namespace UiaComposer
