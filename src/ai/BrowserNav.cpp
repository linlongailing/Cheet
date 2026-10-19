#include "ai/BrowserNav.h"

#include "ai/SiteCatalog.h"
#include "ai/WinInput.h"
#include "platform/UiaCore.h"

#include <QDebug>
#include <QList>
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

/** 激活标签：优先 SelectionItem/Invoke，绝不挪动鼠标指针 */
bool activateTabElement(IUIAutomationElement *el)
{
    if (!el)
        return false;
    IUIAutomationSelectionItemPattern *sel = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                                          IID_IUIAutomationSelectionItemPattern,
                                          reinterpret_cast<void **>(&sel)))
        && sel) {
        const HRESULT hr = sel->Select();
        sel->Release();
        if (SUCCEEDED(hr))
            return true;
    }
    IUIAutomationInvokePattern *inv = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                                          reinterpret_cast<void **>(&inv)))
        && inv) {
        const HRESULT hr = inv->Invoke();
        inv->Release();
        if (SUCCEEDED(hr))
            return true;
    }
    // LegacyIAccessible DoDefaultAction（仍无鼠标）
    IUIAutomationLegacyIAccessiblePattern *leg = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                          IID_IUIAutomationLegacyIAccessiblePattern,
                                          reinterpret_cast<void **>(&leg)))
        && leg) {
        const HRESULT hr = leg->DoDefaultAction();
        leg->Release();
        return SUCCEEDED(hr);
    }
    return false;
}

/** Chrome/Edge/Firefox：Ctrl+1..8 选第 1–8 个标签；更远则 Ctrl+1 再 Ctrl+PageDown */
bool focusTabByIndex(HWND hwnd, int index0)
{
    if (index0 < 0 || !platform::windowAlive(hwnd))
        return false;
    WinInput::setForeground(hwnd);
    if (index0 < 8) {
        WinInput::sendCtrlKey(static_cast<WORD>('1' + index0));
        WinInput::sleepMs(280);
        return true;
    }
    WinInput::sendCtrlKey('1');
    WinInput::sleepMs(220);
    for (int i = 0; i < index0; ++i) {
        if (!platform::windowAlive(hwnd))
            return false;
        WinInput::sendCtrlKey(VK_NEXT);
        WinInput::sleepMs(220);
    }
    return true;
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

/** 不切换标签，只读 UIA 元数据（标题/帮助/值/描述） */
QString tabProbeText(IUIAutomationElement *el)
{
    if (!el)
        return {};
    QStringList parts;
    parts << nameOf(el) << valueOf(el);

    BSTR helpB = nullptr;
    el->get_CurrentHelpText(&helpB);
    if (helpB) {
        parts << QString::fromWCharArray(helpB);
        SysFreeString(helpB);
    }

    IUIAutomationLegacyIAccessiblePattern *leg = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                          IID_IUIAutomationLegacyIAccessiblePattern,
                                          reinterpret_cast<void **>(&leg)))
        && leg) {
        BSTR desc = nullptr;
        BSTR val = nullptr;
        leg->get_CurrentDescription(&desc);
        leg->get_CurrentValue(&val);
        if (desc) {
            parts << QString::fromWCharArray(desc);
            SysFreeString(desc);
        }
        if (val) {
            parts << QString::fromWCharArray(val);
            SysFreeString(val);
        }
        leg->Release();
    }
    return parts.join(QLatin1Char(' ')).toLower();
}

int scoreTabProbe(const QString &probe, const QString &site)
{
    if (probe.isEmpty())
        return 0;
    int score = 0;
    for (const QString &h : SiteCatalog::urlHints(site)) {
        if (probe.contains(h))
            score += 20;
    }
    for (const QString &h : SiteCatalog::searchKeywords(site)) {
        if (probe.contains(h.toLower()))
            score += 15;
    }
    for (const QString &h : SiteCatalog::tabTitleHints(site)) {
        if (probe.contains(h))
            score += 10;
    }
    return score;
}

/**
 * 静默搜标签：只扫 UIA，不 Ctrl+Tab / 不弹标签搜索框。
 * 命中后只激活一次目标标签；失败返回 false（由上层新开页）。
 */
bool findAndActivateTargetTabSilent(HWND hwnd, const QString &site)
{
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return false;

    QList<IUIAutomationElement *> tabs;
    collectTabs(root.get(), 0, tabs);
    if (tabs.isEmpty())
        return false;

    int bestIdx = -1;
    int bestScore = 0;
    for (int i = 0; i < tabs.size(); ++i) {
        const QString probe = tabProbeText(tabs[i]);
        const int score = scoreTabProbe(probe, site);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
        qDebug() << "BrowserNav silent tab[" << i << "] score=" << score << probe.left(80);
    }

    bool ok = false;
    if (bestIdx >= 0 && bestScore > 0) {
        // 只切这一次，不遍历其它标签
        if (!activateTabElement(tabs[bestIdx]))
            focusTabByIndex(hwnd, bestIdx);
        WinInput::sleepMs(400);
        ok = isTargetUrl(readAddressBarUrl(hwnd), site);
        if (!ok) {
            // 标题误匹配时再等一瞬（地址栏刷新）
            WinInput::sleepMs(350);
            ok = isTargetUrl(readAddressBarUrl(hwnd), site);
        }
        qDebug() << "BrowserNav silent activate idx=" << bestIdx << "ok=" << ok;
    }

    for (IUIAutomationElement *tab : tabs)
        tab->Release();
    return ok;
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
    Q_UNUSED(browser);
    if (!platform::windowAlive(hwnd))
        return false;
    const QString s = site.toLower();
    const QString cur = readAddressBarUrl(hwnd);
    qDebug() << "BrowserNav current url=" << cur << "site=" << s;
    if (isTargetUrl(cur, s))
        return true;

    // 静默：UIA 读标签元数据找目标；命中只激活一次。找不到 → false，上层直接新开页
    // 不再 Ctrl+PageDown 扫标签，也不弹 Ctrl+Shift+A 搜索框
    if (!platform::windowAlive(hwnd))
        return false;
    return findAndActivateTargetTabSilent(hwnd, s);
}

bool navigateCurrentTab(HWND hwnd, const QString &url)
{
    Q_UNUSED(hwnd);
    Q_UNUSED(url);
    // 已废弃地址栏键入；请用 WinInput::launchBrowser(browser, url)
    return false;
}

bool openUrlInNewTab(HWND hwnd, const QString &url)
{
    Q_UNUSED(hwnd);
    Q_UNUSED(url);
    // 已废弃 Ctrl+T + 打字；请用 WinInput::launchBrowser(browser, url)
    return false;
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
        // 没有目标标签：用浏览器 --new-tab 直接打开，绝不在地址栏逐字输入
        qDebug() << "BrowserNav no target tab, launchBrowser" << url;
        if (!WinInput::launchBrowser(b, url))
            return nullptr;
        if (launched)
            *launched = true;
        WinInput::sleepMs(1000);
        hwnd = WinInput::findBrowserWindow(b);
        if (!hwnd)
            return nullptr;
        WinInput::setForeground(hwnd);
        found = waitAddressBarTarget(hwnd, s, 20000);
        if (!found && platform::windowAlive(hwnd)) {
            // 新标签可能未置前：再静默匹配一次标题/域名
            found = selectTargetTabByUrl(hwnd, b, s);
        }
        if (!platform::windowAlive(hwnd))
            return nullptr;
    }

    if (!platform::windowAlive(hwnd))
        return nullptr;
    WinInput::setForeground(hwnd);
    return hwnd;
}

#endif

} // namespace BrowserNav
