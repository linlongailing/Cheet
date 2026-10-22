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

IUIAutomationTreeWalker* walker() {
    return platform::UiaCore::instance().controlWalker();
}

QString valueOf(IUIAutomationElement* el) {
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
    return s.trimmed();
}

QString nameOf(IUIAutomationElement* el) {
    if (!el)
        return {};
    BSTR name = nullptr;
    el->get_CurrentName(&name);
    QString s = name ? QString::fromWCharArray(name) : QString();
    if (name)
        SysFreeString(name);
    return s;
}

bool looksLikeUrl(const QString& val) {
    if (val.contains(QLatin1String("://")))
        return true;
    static const QRegularExpression re(QStringLiteral(R"(^[\w-]+(\.[\w-]+)+(:\d+)?(/\S*)?$)"));
    return re.match(val).hasMatch();
}

/** 激活标签：优先 SelectionItem/Invoke，绝不挪动鼠标指针 */
bool activateTabElement(IUIAutomationElement* el) {
    if (!el)
        return false;
    IUIAutomationSelectionItemPattern* sel = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                                          IID_IUIAutomationSelectionItemPattern,
                                          reinterpret_cast<void**>(&sel))) &&
        sel) {
        const HRESULT hr = sel->Select();
        sel->Release();
        if (SUCCEEDED(hr))
            return true;
    }
    IUIAutomationInvokePattern* inv = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(
            UIA_InvokePatternId, IID_IUIAutomationInvokePattern, reinterpret_cast<void**>(&inv))) &&
        inv) {
        const HRESULT hr = inv->Invoke();
        inv->Release();
        if (SUCCEEDED(hr))
            return true;
    }
    // LegacyIAccessible DoDefaultAction（仍无鼠标）
    IUIAutomationLegacyIAccessiblePattern* leg = nullptr;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                          IID_IUIAutomationLegacyIAccessiblePattern,
                                          reinterpret_cast<void**>(&leg))) &&
        leg) {
        const HRESULT hr = leg->DoDefaultAction();
        leg->Release();
        return SUCCEEDED(hr);
    }
    return false;
}

/** Chrome/Edge/Firefox：Ctrl+1..8 选第 1–8 个标签；更远则 Ctrl+1 再 Ctrl+PageDown */
bool focusTabByIndex(HWND hwnd, int index0) {
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

void collectTabs(IUIAutomationElement* el, int depth, QList<IUIAutomationElement*>& tabs) {
    if (!el || depth > 16)
        return;
    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_TabItemControlTypeId) {
        el->AddRef();
        tabs.push_back(el);
    }
    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return;
    IUIAutomationElement* child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        collectTabs(child, depth + 1, tabs);
        IUIAutomationElement* next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

QString walkFindUrl(IUIAutomationElement* el, int depth) {
    if (!el || depth > 16)
        return {};
    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_EditControlTypeId || ctype == UIA_ComboBoxControlTypeId) {
        const QString val = valueOf(el);
        if (!val.isEmpty() && looksLikeUrl(val))
            return val;
    }
    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return {};
    IUIAutomationElement* child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        const QString got = walkFindUrl(child, depth + 1);
        IUIAutomationElement* next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
        if (!got.isEmpty())
            return got;
    }
    return {};
}

IUIAutomationElement* findByAutomationId(IUIAutomationElement* root, const QString& id, int depth) {
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
    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return nullptr;
    IUIAutomationElement* child = nullptr;
    w->GetFirstChildElement(root, &child);
    while (child) {
        IUIAutomationElement* hit = findByAutomationId(child, id, depth + 1);
        IUIAutomationElement* next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
        if (hit)
            return hit;
    }
    return nullptr;
}

IUIAutomationElement* findEditByNames(IUIAutomationElement* el,
                                      const QStringList& names,
                                      int depth) {
    if (!el || depth > 16)
        return nullptr;
    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);
    if (ctype == UIA_EditControlTypeId) {
        const QString name = nameOf(el);
        for (const QString& n : names) {
            if (name == n) {
                el->AddRef();
                return el;
            }
        }
    }
    IUIAutomationTreeWalker* w = walker();
    if (!w)
        return nullptr;
    IUIAutomationElement* child = nullptr;
    w->GetFirstChildElement(el, &child);
    while (child) {
        IUIAutomationElement* hit = findEditByNames(child, names, depth + 1);
        IUIAutomationElement* next = nullptr;
        w->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
        if (hit)
            return hit;
    }
    return nullptr;
}

/**
 * 批量读地址栏：一次 FindAll(Edit) + 缓存属性（名称/位置/值），挑名称命中或
 * 「位置最靠上且值像 URL」的输入框。逐节点走大页面找地址栏太慢时用这个。
 */
QString findAddressUrlFast(IUIAutomationElement* root, const QStringList& names) {
    IUIAutomation* uia = platform::UiaCore::instance().automation();
    if (!uia || !root)
        return {};
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = UIA_EditControlTypeId;
    IUIAutomationCondition* cond = nullptr;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, var, &cond);
    VariantClear(&var);
    if (!cond)
        return {};
    IUIAutomationCacheRequest* cache = nullptr;
    if (FAILED(uia->CreateCacheRequest(&cache)) || !cache) {
        cond->Release();
        return {};
    }
    cache->AddProperty(UIA_NamePropertyId);
    cache->AddProperty(UIA_BoundingRectanglePropertyId);
    IUIAutomationElementArray* arr = nullptr;
    const HRESULT hr = root->FindAllBuildCache(TreeScope_Descendants, cond, cache, &arr);
    cond->Release();
    cache->Release();
    if (FAILED(hr) || !arr)
        return {};

    QString bestUrl;
    LONG bestTop = 0;
    bool haveCandidate = false;
    int len = 0;
    arr->get_Length(&len);
    for (int i = 0; i < len; ++i) {
        IUIAutomationElement* el = nullptr;
        if (FAILED(arr->GetElement(i, &el)) || !el)
            continue;
        BSTR nameB = nullptr;
        RECT br{};
        el->get_CachedName(&nameB);
        el->get_CachedBoundingRectangle(&br);
        const QString name = nameB ? QString::fromWCharArray(nameB) : QString();
        if (nameB)
            SysFreeString(nameB);
        // 候选：名称是地址栏，或形状像地址栏（宽、矮的单行框）。候选很少，
        // 逐个读当前值即可，不必把每个 Edit 都跨进程读一遍
        const bool named = names.contains(name);
        const LONG w = br.right - br.left;
        const LONG h = br.bottom - br.top;
        const QString val = (named || (w > 200 && h >= 12 && h <= 60)) ? valueOf(el) : QString();
        el->Release();
        if (val.isEmpty())
            continue;
        if (named) {
            arr->Release();
            return val;
        }
        if (looksLikeUrl(val) && (!haveCandidate || br.top < bestTop)) {
            haveCandidate = true;
            bestTop = br.top;
            bestUrl = val;
        }
    }
    arr->Release();
    return bestUrl;
}

/**
 * 按网址搜标签（所有浏览器同一逻辑）：标签元素本身不暴露 URL（只有标题，
 * 对话页标题常不含站点名，靠标题匹配必然漏检），所以逐个静默激活标签
 * （UIA Select，不动鼠标/键盘）、读地址栏 URL 与目标站比对，找到即停。
 * 调用方已确认当前选中标签的 URL 不是目标站，这里跳过它。
 */
bool findAndActivateTargetTabByUrl(HWND hwnd, const QString& site) {
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return false;

    QList<IUIAutomationElement*> tabs;
    collectTabs(root.get(), 0, tabs);
    if (tabs.isEmpty())
        return false;

    bool ok = false;
    for (int i = 0; i < tabs.size(); ++i) {
        if (!platform::windowAlive(hwnd))
            break;
        IUIAutomationSelectionItemPattern* sel = nullptr;
        BOOL isSelected = FALSE;
        if (SUCCEEDED(tabs[i]->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                                                   IID_IUIAutomationSelectionItemPattern,
                                                   reinterpret_cast<void**>(&sel))) &&
            sel) {
            sel->get_CurrentIsSelected(&isSelected);
            sel->Release();
        }
        if (isSelected)
            continue;

        if (!activateTabElement(tabs[i]))
            focusTabByIndex(hwnd, i);
        // 地址栏在切标签后很快刷新：先等一小段读一次，读不到再补一次
        WinInput::sleepMs(180);
        if (!platform::windowAlive(hwnd))
            break;
        QString url = readAddressBarUrl(hwnd);
        if (url.isEmpty()) {
            WinInput::sleepMs(300);
            if (!platform::windowAlive(hwnd))
                break;
            url = readAddressBarUrl(hwnd);
        }
        qDebug() << "BrowserNav url-scan tab[" << i << "]" << url;
        if (isTargetUrl(url, site)) {
            ok = true;
            break;
        }
    }

    for (IUIAutomationElement* tab : tabs)
        tab->Release();
    qDebug() << "BrowserNav url-scan done ok=" << ok;
    return ok;
}

} // namespace

bool isTargetUrl(const QString& url, const QString& site) {
    QString u = url;
    u.remove(QChar(0x200b));
    u = u.trimmed().toLower();
    for (const QString& h : SiteCatalog::urlHints(site)) {
        if (u.contains(h))
            return true;
    }
    const QUrl parsed = QUrl::fromUserInput(url);
    if (parsed.isValid()) {
        QString host = parsed.host().toLower();
        if (host.startsWith(QLatin1String("www.")))
            host = host.mid(4);
        for (const QString& h : SiteCatalog::urlHints(site)) {
            if (host == h || host.endsWith(QLatin1Char('.') + h))
                return true;
        }
    }
    return false;
}

QString readAddressBarUrl(HWND hwnd) {
    auto root = platform::UiaCore::instance().elementFromHwnd(hwnd);
    if (!root)
        return {};

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

    // 先批量找（大页面快得多）；找不到再走旧的逐节点兜底
    const QString fast = findAddressUrlFast(root.get(), names);
    if (!fast.isEmpty())
        return fast;

    if (IUIAutomationElement* ff =
            findByAutomationId(root.get(), QStringLiteral("urlbar-input"), 0)) {
        QString v = valueOf(ff);
        ff->Release();
        if (!v.isEmpty())
            return v;
    }

    if (IUIAutomationElement* edit = findEditByNames(root.get(), names, 0)) {
        QString v = valueOf(edit);
        edit->Release();
        if (!v.isEmpty())
            return v;
    }

    return walkFindUrl(root.get(), 0);
}

bool selectTargetTabByUrl(HWND hwnd, const QString& browser, const QString& site) {
    Q_UNUSED(browser);
    if (!platform::windowAlive(hwnd))
        return false;
    const QString s = site.toLower();
    const QString cur = readAddressBarUrl(hwnd);
    qDebug() << "BrowserNav current url=" << cur << "site=" << s;
    if (isTargetUrl(cur, s))
        return true;

    // 按网址找：逐个静默激活标签读地址栏比对（不动鼠标/键盘，不弹标签搜索框）。
    // 找不到 → false，上层直接新开页
    if (!platform::windowAlive(hwnd))
        return false;
    return findAndActivateTargetTabByUrl(hwnd, s);
}

bool navigateCurrentTab(HWND hwnd, const QString& url) {
    Q_UNUSED(hwnd);
    Q_UNUSED(url);
    // 已废弃地址栏键入；请用 WinInput::launchBrowser(browser, url)
    return false;
}

bool openUrlInNewTab(HWND hwnd, const QString& url) {
    Q_UNUSED(hwnd);
    Q_UNUSED(url);
    // 已废弃 Ctrl+T + 打字；请用 WinInput::launchBrowser(browser, url)
    return false;
}

bool waitAddressBarTarget(HWND hwnd, const QString& site, int timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(qMax(1000, timeoutMs));
    int i = 0;
    while (GetTickCount64() < deadline) {
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

HWND focusOrOpenTarget(const QString& browser,
                       const QString& site,
                       const QString& url,
                       bool* launched) {
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
    // 已打开的浏览器不空等：直接读标签，输入框等待在后续环节兜底
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
