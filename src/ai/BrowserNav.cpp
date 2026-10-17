#include "ai/BrowserNav.h"

#include "ai/SiteCatalog.h"
#include "ai/WinInput.h"
#include "platform/UiaCore.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QList>
#include <QMetaObject>
#include <QRegularExpression>
#include <QUrl>

namespace BrowserNav {

#ifdef Q_OS_WIN

namespace {

IUIAutomationTreeWalker *walker()
{
    return platform::UiaCore::instance().controlWalker();
}

QString valueOf(IUIAutomationElement *el)
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
    return s.trimmed();
}

QString nameOf(IUIAutomationElement *el)
{
    if (!el)
        return {};
    BSTR name = nullptr;
    el->get_CurrentName(&name);
    QString s = name ? QString::fromWCharArray(name) : QString();
    if (name)
        SysFreeString(name);
    return s;
}

bool looksLikeUrl(const QString &val)
{
    if (val.contains(QLatin1String("://")))
        return true;
    static const QRegularExpression re(QStringLiteral(R"(^[\w-]+(\.[\w-]+)+(:\d+)?(/\S*)?$)"));
    return re.match(val).hasMatch();
}

void setClipboardText(const QString &text)
{
    QMetaObject::invokeMethod(
        qApp, [text]() { QApplication::clipboard()->setText(text); }, Qt::BlockingQueuedConnection);
}

void clickElement(IUIAutomationElement *el)
{
    if (!el)
        return;
    RECT br{};
    el->get_CurrentBoundingRectangle(&br);
    if (br.right > br.left && br.bottom > br.top) {
        const int cx = (br.left + br.right) / 2;
        const int cy = (br.top + br.bottom) / 2;
        SetCursorPos(cx, cy);
        INPUT ins[2]{};
        ins[0].type = INPUT_MOUSE;
        ins[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        ins[1].type = INPUT_MOUSE;
        ins[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, ins, sizeof(INPUT));
        return;
    }
    IUIAutomationInvokePattern *inv = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                                          reinterpret_cast<void **>(&inv)))
        && inv) {
        inv->Invoke();
        inv->Release();
    }
}

void collectTabs(IUIAutomationElement *el, int depth, QList<IUIAutomationElement *> &tabs)
{
    if (!el || depth > 16)
        return;
    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_TabItemControlTypeId) {
        el->AddRef();
        tabs.push_back(el);
    }
    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return;
    IUIAutomationElement *child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        collectTabs(child, depth + 1, tabs);
        IUIAutomationElement *next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

QString walkFindUrl(IUIAutomationElement *el, int depth)
{
    if (!el || depth > 16)
        return {};
    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_EditControlTypeId || ctype == UIA_ComboBoxControlTypeId) {
        const QString val = valueOf(el);
        if (!val.isEmpty() && looksLikeUrl(val))
            return val;
    }
    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return {};
    IUIAutomationElement *child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        const QString got = walkFindUrl(child, depth + 1);
        IUIAutomationElement *next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
        if (!got.isEmpty())
            return got;
    }
    return {};
}

IUIAutomationElement *findByAutomationId(IUIAutomationElement *root, const QString &id, int depth)
{
    if (!root || depth > 10)
        return nullptr;
    BSTR aid = nullptr;
    root->get_CurrentAutomationId(&aid);
    const QString cur = aid ? QString::fromWCharArray(aid) : QString();
    if (aid)
        SysFreeString(aid);
    if (cur == id) {
        root->AddRef();
        return root;
    }
    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return nullptr;
    IUIAutomationElement *child = nullptr;
    w->GetFirstChildElement(root, &child);
    while (child) {
        IUIAutomationElement *hit = findByAutomationId(child, id, depth + 1);
        IUIAutomationElement *next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
        if (hit)
            return hit;
    }
    return nullptr;
}

IUIAutomationElement *findEditByNames(IUIAutomationElement *el, const QStringList &names, int depth)
{
    if (!el || depth > 16)
        return nullptr;
    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_EditControlTypeId) {
        const QString name = nameOf(el);
        for (const QString &n : names) {
            if (name == n) {
                el->AddRef();
                return el;
            }
        }
    }
    IUIAutomationTreeWalker *w = walker();
    if (!w)
        return nullptr;
    IUIAutomationElement *child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        IUIAutomationElement *hit = findEditByNames(child, names, depth + 1);
        IUIAutomationElement *next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
        if (hit)
            return hit;
    }
    return nullptr;
}

bool scanTabsLeftToRight(HWND hwnd, const QString &browser, const QString &site)
{
    Q_UNUSED(browser);
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    QList<IUIAutomationElement *> tabs;
    if (root)
        collectTabs(root.get(), 0, tabs);
    const int n = tabs.size();
    const int limit = n > 0 ? n : 50;
    for (IUIAutomationElement *t : tabs)
        t->Release();

    if (!platform::windowAlive(hwnd))
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sendCtrlKey('1');
    WinInput::sleepMs(300);
    QString prev;
    for (int i = 0; i < limit; ++i) {
        if (!platform::windowAlive(hwnd))
            return false;
        const QString url = readAddressBarUrl(hwnd);
        qDebug() << "BrowserNav scan[" << i << "] url=" << url;
        if (isTargetUrl(url, site))
            return true;
        if (n == 0 && !url.isEmpty() && url == prev)
            break;
        prev = url;
        if (i < limit - 1) {
            // Ctrl+PageDown
            WinInput::sendCtrlKey(VK_NEXT);
            WinInput::sleepMs(280);
        }
    }
    return false;
}

bool clickTabsUntilTarget(HWND hwnd, const QString &site)
{
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return false;
    QList<IUIAutomationElement *> tabs;
    collectTabs(root.get(), 0, tabs);
    WinInput::setForeground(hwnd);
    bool found = false;
    for (IUIAutomationElement *tab : tabs) {
        clickElement(tab);
        WinInput::sleepMs(300);
        if (isTargetUrl(readAddressBarUrl(hwnd), site)) {
            found = true;
            break;
        }
    }
    for (IUIAutomationElement *tab : tabs)
        tab->Release();
    return found;
}

bool clickTabByTitle(HWND hwnd, const QString &site)
{
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return false;
    QList<IUIAutomationElement *> tabs;
    collectTabs(root.get(), 0, tabs);
    const QStringList hints = SiteCatalog::tabTitleHints(site);
    bool found = false;
    for (IUIAutomationElement *tab : tabs) {
        const QString low = nameOf(tab).toLower();
        bool hit = false;
        for (const QString &h : hints) {
            if (low.contains(h)) {
                hit = true;
                break;
            }
        }
        if (!hit)
            continue;
        clickElement(tab);
        WinInput::sleepMs(350);
        if (isTargetUrl(readAddressBarUrl(hwnd), site)) {
            found = true;
            break;
        }
        WinInput::sleepMs(350);
        if (isTargetUrl(readAddressBarUrl(hwnd), site)) {
            found = true;
            break;
        }
    }
    for (IUIAutomationElement *tab : tabs)
        tab->Release();
    return found;
}

} // namespace

bool isTargetUrl(const QString &url, const QString &site)
{
    QString u = url;
    u.remove(QChar(0x200b));
    u = u.trimmed().toLower();
    for (const QString &h : SiteCatalog::urlHints(site)) {
        if (u.contains(h))
            return true;
    }
    const QUrl parsed = QUrl::fromUserInput(url);
    if (parsed.isValid()) {
        QString host = parsed.host().toLower();
        if (host.startsWith(QLatin1String("www.")))
            host = host.mid(4);
        for (const QString &h : SiteCatalog::urlHints(site)) {
            if (host == h || host.endsWith(QLatin1Char('.') + h))
                return true;
        }
    }
    return false;
}

QString readAddressBarUrl(HWND hwnd)
{
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return {};

    if (IUIAutomationElement *ff = findByAutomationId(root.get(), QStringLiteral("urlbar-input"), 0)) {
        const QString v = valueOf(ff);
        ff->Release();
        if (!v.isEmpty())
            return v;
    }

    static const QStringList names = {
        QStringLiteral("地址和搜索栏"),
        QStringLiteral("Address and search bar"),
        QStringLiteral("Address bar"),
        QStringLiteral("Search or enter web address"),
        QStringLiteral("Search with Google or enter address"),
        QStringLiteral("Search or enter address"),
        QStringLiteral("使用 Google 搜索，或者输入网址"),
        QStringLiteral("搜索或输入网址"),
    };
    if (IUIAutomationElement *edit = findEditByNames(root.get(), names, 0)) {
        const QString v = valueOf(edit);
        edit->Release();
        if (!v.isEmpty())
            return v;
    }

    return walkFindUrl(root.get(), 0);
}

bool selectTargetTabByUrl(HWND hwnd, const QString &browser, const QString &site)
{
    if (!platform::windowAlive(hwnd))
        return false;
    const QString s = site.toLower();
    const QString b = browser.toLower();
    const QString cur = readAddressBarUrl(hwnd);
    qDebug() << "BrowserNav current url=" << cur << "site=" << s;
    if (isTargetUrl(cur, s))
        return true;

    WinInput::setForeground(hwnd);
    WinInput::sleepMs(150);
    if (!platform::windowAlive(hwnd))
        return false;

    if (scanTabsLeftToRight(hwnd, b, s))
        return true;
    if (!platform::windowAlive(hwnd))
        return false;
    if (clickTabsUntilTarget(hwnd, s))
        return true;
    if (!platform::windowAlive(hwnd))
        return false;
    if (clickTabByTitle(hwnd, s))
        return true;
    if (!platform::windowAlive(hwnd))
        return false;

    // Ctrl+Shift+A：Edge/Chrome 标签搜索；Firefox 是附加组件，跳过
    if (b != QLatin1String("firefox")) {
        for (const QString &keyword : SiteCatalog::searchKeywords(s)) {
            if (!platform::windowAlive(hwnd))
                return false;
            WinInput::sendKey(VK_ESCAPE);
            WinInput::sleepMs(100);
            WinInput::sendCtrlShiftKey('A');
            WinInput::sleepMs(550);
            WinInput::sendCtrlKey('A');
            WinInput::sleepMs(50);
            setClipboardText(keyword);
            WinInput::sendCtrlKey('V');
            WinInput::sleepMs(500);
            WinInput::sendKey(VK_DOWN);
            WinInput::sleepMs(120);
            WinInput::sendEnter();
            WinInput::sleepMs(400);
            if (!platform::windowAlive(hwnd))
                return false;
            if (isTargetUrl(readAddressBarUrl(hwnd), s))
                return true;
            WinInput::sendKey(VK_ESCAPE);
            WinInput::sleepMs(100);
        }
    }
    if (!platform::windowAlive(hwnd))
        return false;
    return isTargetUrl(readAddressBarUrl(hwnd), s);
}

bool openUrlInNewTab(HWND hwnd, const QString &url)
{
    if (!hwnd || url.isEmpty())
        return false;
    WinInput::setForeground(hwnd);
    WinInput::sleepMs(100);
    WinInput::sendCtrlKey('T');
    WinInput::sleepMs(350);
    WinInput::sendCtrlKey('L');
    WinInput::sleepMs(200);
    setClipboardText(url);
    WinInput::sendCtrlKey('V');
    WinInput::sleepMs(120);
    WinInput::sendEnter();
    return true;
}

bool waitAddressBarTarget(HWND hwnd, const QString &site, int timeoutMs)
{
    const qint64 deadline = GetTickCount64() + qMax(1000, timeoutMs);
    int i = 0;
    while (static_cast<qint64>(GetTickCount64()) < deadline) {
        if (!platform::windowAlive(hwnd))
            return false;
        const QString url = readAddressBarUrl(hwnd);
        if (i % 4 == 0)
            qDebug() << "BrowserNav wait url[" << i << "]" << url;
        if (isTargetUrl(url, site)) {
            WinInput::sleepMs(800);
            return true;
        }
        WinInput::sleepMs(250);
        ++i;
    }
    return isTargetUrl(readAddressBarUrl(hwnd), site);
}

HWND focusOrOpenTarget(const QString &browser, const QString &site, const QString &url, bool *launched)
{
    if (launched)
        *launched = false;
    const QString b = browser.toLower();
    const QString s = site.toLower();

    HWND hwnd = WinInput::findBrowserWindow(b);
    if (!hwnd) {
        qDebug() << "BrowserNav no browser, launch" << b << url;
        if (!WinInput::launchBrowser(b, url))
            return nullptr;
        for (int i = 0; i < 50 && !hwnd; ++i) {
            WinInput::sleepMs(300);
            hwnd = WinInput::findBrowserWindow(b);
        }
        if (!hwnd)
            return nullptr;
        if (launched)
            *launched = true;
        WinInput::setForeground(hwnd);
        waitAddressBarTarget(hwnd, s, 20000);
        return hwnd;
    }

    WinInput::setForeground(hwnd);
    WinInput::sleepMs(150);
    if (!platform::windowAlive(hwnd))
        return nullptr;

    bool found = false;
    try {
        found = selectTargetTabByUrl(hwnd, b, s);
    } catch (...) {
        found = false;
    }
    if (!platform::windowAlive(hwnd))
        return nullptr;

    if (!found) {
        qDebug() << "BrowserNav no target tab, open new tab" << url;
        WinInput::setForeground(hwnd);
        openUrlInNewTab(hwnd, url);
        if (!platform::windowAlive(hwnd))
            return nullptr;
        found = waitAddressBarTarget(hwnd, s, 15000);
        if (!platform::windowAlive(hwnd))
            return nullptr;
        if (!found) {
            // 兜底：外部再开一次
            WinInput::launchBrowser(b, url);
            WinInput::sleepMs(1500);
            hwnd = WinInput::findBrowserWindow(b);
            if (!hwnd)
                return nullptr;
            waitAddressBarTarget(hwnd, s, 15000);
            if (!platform::windowAlive(hwnd))
                return nullptr;
        }
    }

    if (!platform::windowAlive(hwnd))
        return nullptr;
    WinInput::setForeground(hwnd);
    return hwnd;
}

#endif

} // namespace BrowserNav
