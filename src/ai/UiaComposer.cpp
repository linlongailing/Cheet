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

const QStringList kUploadingHints = {
    QStringLiteral("上传中"), QStringLiteral("正在上传"), QStringLiteral("正在加载"),
    QStringLiteral("处理中"), QStringLiteral("uploading"), QStringLiteral("loading"),
    QStringLiteral("processing")};
const QStringList kSendHints = {QStringLiteral("发送"), QStringLiteral("send")};
const QStringList kRemoveHints = {QStringLiteral("移除"), QStringLiteral("删除"), QStringLiteral("remove")};

QString norm(const QString &s)
{
    return s.trimmed().toLower();
}

bool containsAny(const QString &text, const QStringList &hints)
{
    for (const QString &h : hints) {
        if (text.contains(h))
            return true;
    }
    return false;
}

RECT windowRect(HWND hwnd)
{
    RECT r{};
    GetWindowRect(hwnd, &r);
    return r;
}

IUIAutomationTreeWalker *walker()
{
    return platform::UiaCore::instance().controlWalker();
}

struct ComposerHit {
    IUIAutomationElement *el = nullptr;
    int matched = 1; // 0=matched hint
    LONG bottom = 0;
};

void walkFindComposer(IUIAutomationElement *el, const RECT &win, LONG contentTop, const QStringList &hints,
                      int depth, QList<ComposerHit> &hits)
{
    if (!el || depth > 70)
        return;

    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_EditControlTypeId) {
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

        const bool visible = (br.right - br.left > 20) && (br.bottom - br.top > 8);
        const bool notOmni = cls != QLatin1String("OmniboxViewViews") && cls != QLatin1String("urlbar-input");
        if (visible && notOmni && br.top > contentTop && br.left >= win.left - 2 && br.right <= win.right + 8) {
            const QString text = norm(name + QLatin1Char(' ') + cls);
            ComposerHit hit;
            hit.el = el;
            hit.el->AddRef();
            hit.matched = containsAny(text, hints) ? 0 : 1;
            hit.bottom = br.bottom;
            hits.push_back(hit);
        }
    }

    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return;
    IUIAutomationElement *child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        walkFindComposer(child, win, contentTop, hints, depth + 1, hits);
        IUIAutomationElement *next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

IUIAutomationElement *findComposerCtrl(HWND hwnd, const QString &site)
{
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return nullptr;
    const RECT win = windowRect(hwnd);
    const LONG contentTop = win.top + 100;
    QList<ComposerHit> hits;
    walkFindComposer(root.get(), win, contentTop, SiteCatalog::composerHints(site), 0, hits);
    if (hits.isEmpty())
        return nullptr;
    std::sort(hits.begin(), hits.end(), [](const ComposerHit &a, const ComposerHit &b) {
        if (a.matched != b.matched)
            return a.matched < b.matched;
        return a.bottom > b.bottom;
    });
    IUIAutomationElement *best = hits.first().el;
    for (int i = 1; i < hits.size(); ++i)
        hits[i].el->Release();
    return best;
}

IUIAutomationElement *composerContainer(IUIAutomationElement *composer, HWND hwnd)
{
    if (!composer)
        return nullptr;
    const RECT win = windowRect(hwnd);
    const LONG maxH = static_cast<LONG>((win.bottom - win.top) * 0.45);
    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return nullptr;

    IUIAutomationElement *container = composer;
    container->AddRef();
    for (int i = 0; i < 12; ++i) {
        IUIAutomationElement *parent = nullptr;
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

void scanTree(IUIAutomationElement *el, int depth, State &state, LONG &sendBottom, int &removeButtons,
              int &thumbnails)
{
    if (!el || depth > 30)
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
        QString name = norm((nameB ? QString::fromWCharArray(nameB) : QString()) + QLatin1Char(' ')
                            + (helpB ? QString::fromWCharArray(helpB) : QString()));
        if (nameB)
            SysFreeString(nameB);
        if (helpB)
            SysFreeString(helpB);

        BOOL enabled = TRUE;
        el->get_CurrentIsEnabled(&enabled);

        if (ctype == UIA_ProgressBarControlTypeId
            || (name.size() <= 40 && containsAny(name, kUploadingHints))) {
            state.uploading = true;
        }
        if (ctype == UIA_ButtonControlTypeId && containsAny(name, kRemoveHints))
            removeButtons++;
        if (ctype == UIA_ImageControlTypeId && !name.trimmed().isEmpty())
            thumbnails++;
        if (ctype == UIA_ButtonControlTypeId && name.size() <= 40 && containsAny(name, kSendHints)
            && br.bottom > sendBottom) {
            sendBottom = br.bottom;
            state.sendFound = true;
            state.sendEnabled = enabled;
        }
    }

    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return;
    IUIAutomationElement *child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        scanTree(child, depth + 1, state, sendBottom, removeButtons, thumbnails);
        IUIAutomationElement *next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

void clickPoint(int x, int y)
{
    SetCursorPos(x, y);
    INPUT ins[2]{};
    ins[0].type = INPUT_MOUSE;
    ins[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    ins[1].type = INPUT_MOUSE;
    ins[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, ins, sizeof(INPUT));
}

QString readEditValue(IUIAutomationElement *el)
{
    if (!el)
        return {};
    IUIAutomationValuePattern *pat = nullptr;
    if (FAILED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                                       reinterpret_cast<void **>(&pat)))
        || !pat)
        return {};
    BSTR val = nullptr;
    pat->get_CurrentValue(&val);
    QString s = val ? QString::fromWCharArray(val) : QString();
    if (val)
        SysFreeString(val);
    pat->Release();
    return s;
}

void invokeRemoveButtons(IUIAutomationElement *container)
{
    if (!container)
        return;
    // 简化：扫描并 Invoke 移除按钮
    struct Item {
        IUIAutomationElement *el;
    };
    QList<Item> buttons;

    std::function<void(IUIAutomationElement *, int)> walk;
    walk = [&](IUIAutomationElement *el, int depth) {
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
        IUIAutomationTreeWalker *w = walker();
        if (!w)
            return;
        IUIAutomationElement *child = nullptr;
        w->GetFirstChildElement(el, &child);
        while (child) {
            walk(child, depth + 1);
            IUIAutomationElement *next = nullptr;
            w->GetNextSiblingElement(child, &next);
            child->Release();
            child = next;
        }
    };
    walk(container, 0);

    for (Item &it : buttons) {
        IUIAutomationInvokePattern *inv = nullptr;
        if (SUCCEEDED(it.el->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                                                 reinterpret_cast<void **>(&inv)))
            && inv) {
            inv->Invoke();
            inv->Release();
        }
        it.el->Release();
        WinInput::sleepMs(200);
    }
}

} // namespace

void enableWebAccessibility(HWND hwnd)
{
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
            auto *list = reinterpret_cast<QList<HWND> *>(lp);
            wchar_t cls[128]{};
            GetClassNameW(child, cls, 128);
            const QString c = QString::fromWCharArray(cls);
            if (c == QLatin1String("Chrome_RenderWidgetHostHWND")
                || c == QLatin1String("MozillaWindowClass"))
                list->push_back(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&targets));
    targets.push_back(hwnd);

    const IID iidAcc = IID_IAccessible;
    for (HWND t : targets) {
        IAccessible *acc = nullptr;
        if (AccessibleObjectFromWindow(t, OBJID_CLIENT, iidAcc, reinterpret_cast<void **>(&acc)) == S_OK
            && acc) {
            acc->Release();
        }
    }
}

bool focusComposer(HWND hwnd, const QString &site)
{
    enableWebAccessibility(hwnd);
    IUIAutomationElement *ctrl = findComposerCtrl(hwnd, site);
    if (!ctrl)
        return false;
    RECT br{};
    const HRESULT hr = ctrl->get_CurrentBoundingRectangle(&br);
    ctrl->Release();
    if (FAILED(hr) || br.right <= br.left || br.bottom <= br.top)
        return false;
    const int cx = (br.left + br.right) / 2;
    const int cy = (br.top + br.bottom) / 2;
    clickPoint(cx, cy);
    WinInput::sleepMs(120);
    return true;
}

State scan(HWND hwnd, const QString &site)
{
    State state;
    enableWebAccessibility(hwnd);
    IUIAutomationElement *composer = findComposerCtrl(hwnd, site);
    if (!composer)
        return state;
    IUIAutomationElement *container = composerContainer(composer, hwnd);
    composer->Release();
    if (!container)
        return state;

    state.composerFound = true;
    LONG sendBottom = -1;
    int removeButtons = 0;
    int thumbnails = 0;
    scanTree(container, 0, state, sendBottom, removeButtons, thumbnails);
    state.attachments = qMax(removeButtons, thumbnails);
    container->Release();
    return state;
}

bool waitAttachmentAdded(HWND hwnd, const QString &site, int before, int timeoutMs)
{
    const qint64 deadline = GetTickCount64() + timeoutMs;
    while (static_cast<qint64>(GetTickCount64()) < deadline) {
        if (!platform::windowAlive(hwnd))
            return false;
        WinInput::sleepMs(400);
        if (!platform::windowAlive(hwnd))
            return false;
        const State st = scan(hwnd, site);
        if (st.attachments > before || (st.uploading && before == 0))
            return true;
    }
    return false;
}

bool waitUntilUploaded(HWND hwnd, const QString &site, int minWaitMs, int maxWaitMs)
{
    qDebug() << "UIA wait upload min=" << minWaitMs << "max=" << maxWaitMs;
    if (!platform::windowAlive(hwnd))
        return false;
    const qint64 start = GetTickCount64();
    WinInput::sleepMs(800);
    if (!platform::windowAlive(hwnd))
        return false;
    const qint64 deadline = start + qMax(10000, maxWaitMs);
    int readyStreak = 0;

    while (static_cast<qint64>(GetTickCount64()) < deadline) {
        if (!platform::windowAlive(hwnd)) {
            qDebug() << "UIA upload wait aborted: window closed";
            return false;
        }
        const State st = scan(hwnd, site);
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

bool messageSent(HWND hwnd, const QString &site, const QString &prompt)
{
    enableWebAccessibility(hwnd);
    IUIAutomationElement *ctrl = findComposerCtrl(hwnd, site);
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

void clearComposer(HWND hwnd, const QString &site)
{
    enableWebAccessibility(hwnd);
    IUIAutomationElement *composer = findComposerCtrl(hwnd, site);
    if (composer) {
        IUIAutomationElement *container = composerContainer(composer, hwnd);
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
