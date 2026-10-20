#include "ai/WinInput.h"

#include "ai/SiteCatalog.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#ifdef Q_OS_WIN
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace WinInput {

#ifdef Q_OS_WIN

namespace {

/** 工作线程 SendInput 必须 Attach 到前台线程，否则浏览器收不到键鼠 */
struct ForegroundInputScope {
    DWORD foreTid = 0;
    DWORD selfTid = 0;
    bool attached = false;

    explicit ForegroundInputScope(HWND preferHwnd = nullptr) {
        selfTid = GetCurrentThreadId();
        HWND fg = preferHwnd && IsWindow(preferHwnd) ? preferHwnd : GetForegroundWindow();
        if (!fg)
            return;
        foreTid = GetWindowThreadProcessId(fg, nullptr);
        if (foreTid && foreTid != selfTid)
            attached = AttachThreadInput(selfTid, foreTid, TRUE) != FALSE;
    }

    ~ForegroundInputScope() {
        if (attached)
            AttachThreadInput(selfTid, foreTid, FALSE);
    }
};

} // namespace

void sleepMs(int ms) {
    QThread::msleep(static_cast<unsigned long>(ms));
}

bool setForeground(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd))
        return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    const HWND prev = GetForegroundWindow();
    const DWORD foreTid = prev ? GetWindowThreadProcessId(prev, nullptr) : 0;
    const DWORD selfTid = GetCurrentThreadId();
    const DWORD targetTid = GetWindowThreadProcessId(hwnd, nullptr);
    bool a1 = false;
    bool a2 = false;
    if (foreTid && foreTid != selfTid)
        a1 = AttachThreadInput(selfTid, foreTid, TRUE) != FALSE;
    if (targetTid && targetTid != selfTid && targetTid != foreTid)
        a2 = AttachThreadInput(selfTid, targetTid, TRUE) != FALSE;

    AllowSetForegroundWindow(ASFW_ANY);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    if (a2)
        AttachThreadInput(selfTid, targetTid, FALSE);
    if (a1)
        AttachThreadInput(selfTid, foreTid, FALSE);

    sleepMs(40);
    return GetForegroundWindow() == hwnd || IsWindow(hwnd);
}

static void sendVk(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

void sendCtrlKey(WORD vk) {
    ForegroundInputScope scope;
    sendVk(VK_CONTROL, false);
    sleepMs(20);
    sendVk(vk, false);
    sleepMs(20);
    sendVk(vk, true);
    sleepMs(20);
    sendVk(VK_CONTROL, true);
    sleepMs(40);
}

void sendCtrlShiftKey(WORD vk) {
    ForegroundInputScope scope;
    sendVk(VK_CONTROL, false);
    sendVk(VK_SHIFT, false);
    sleepMs(20);
    sendVk(vk, false);
    sleepMs(20);
    sendVk(vk, true);
    sleepMs(20);
    sendVk(VK_SHIFT, true);
    sendVk(VK_CONTROL, true);
    sleepMs(40);
}

void sendKey(WORD vk) {
    ForegroundInputScope scope;
    sendVk(vk, false);
    sleepMs(20);
    sendVk(vk, true);
    sleepMs(40);
}

void sendEnter() {
    sendKey(VK_RETURN);
}

void clickScreen(int x, int y, bool moveCursor) {
    POINT screen{x, y};

    if (!moveCursor) {
        // 静默点击：不 SetCursorPos，用户指针零闪烁
        HWND hwnd = WindowFromPoint(screen);
        if (!hwnd)
            return;
        HWND root = GetAncestor(hwnd, GA_ROOT);
        ForegroundInputScope scope(root ? root : hwnd);

        // 从根窗口再取一次深层子窗，提高 Chromium 命中率
        if (root) {
            POINT rp = screen;
            ScreenToClient(root, &rp);
            if (HWND deep = ChildWindowFromPointEx(root, rp, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED))
                hwnd = deep;
        }

        POINT client = screen;
        if (!ScreenToClient(hwnd, &client))
            return;
        const LPARAM lp = MAKELPARAM(static_cast<WORD>(client.x), static_cast<WORD>(client.y));
        SendMessageW(hwnd, WM_MOUSEMOVE, 0, lp);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        sleepMs(40);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, lp);
        sleepMs(60);
        return;
    }

    // 兜底：真实指针点击（会闪，尽量不用）
    POINT saved{};
    GetCursorPos(&saved);
    ForegroundInputScope scope;
    SetCursorPos(x, y);
    sleepMs(40);
    INPUT ins[2]{};
    ins[0].type = INPUT_MOUSE;
    ins[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    ins[1].type = INPUT_MOUSE;
    ins[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, ins, sizeof(INPUT));
    sleepMs(80);
    SetCursorPos(saved.x, saved.y);
}

void sendText(const QString& text) {
    ForegroundInputScope scope;
    for (const QChar ch : text) {
        INPUT in[2]{};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wScan = ch.unicode();
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1] = in[0];
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        sleepMs(5);
    }
}

struct EnumCtx {
    QString browser;
    QString site;
    HWND found = nullptr;
    LONG bestArea = 0;
};

static QString windowTitle(HWND hwnd) {
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return QString::fromWCharArray(buf);
}

static QString className(HWND hwnd) {
    wchar_t buf[256]{};
    GetClassNameW(hwnd, buf, 256);
    return QString::fromWCharArray(buf);
}

static QString processName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, path, &size))
        name = QFileInfo(QString::fromWCharArray(path)).fileName().toLower();
    CloseHandle(h);
    return name;
}

static bool isTopLevelVisible(HWND hwnd) {
    return IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER);
}

static BOOL CALLBACK enumBrowserProc(HWND hwnd, LPARAM lParam) {
    // Win32 EnumWindows 回调经 LPARAM 传上下文指针，为系统约定用法
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    if (!isTopLevelVisible(hwnd))
        return TRUE;
    const QString title = windowTitle(hwnd).toLower();
    const QString cls = className(hwnd);
    const QString exe = processName(hwnd);
    if (title.isEmpty())
        return TRUE;

    if (ctx->browser == QLatin1String("firefox")) {
        if (cls == QLatin1String("MozillaWindowClass") &&
            (title.contains(QLatin1String("firefox")) || exe == QLatin1String("firefox.exe"))) {
            ctx->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    if (cls != QLatin1String("Chrome_WidgetWin_1"))
        return TRUE;

    if (ctx->browser == QLatin1String("chrome")) {
        if (title.contains(QLatin1String("microsoft edge")))
            return TRUE;
        if (title.contains(QLatin1String("google chrome")) || exe == QLatin1String("chrome.exe")) {
            ctx->found = hwnd;
            return FALSE;
        }
    } else { // edge
        if (title.contains(QLatin1String("google chrome")) ||
            title.contains(QLatin1String("brave")))
            return TRUE;
        if (title.contains(QLatin1String("edge")) || exe == QLatin1String("msedge.exe")) {
            ctx->found = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND findBrowserWindow(const QString& browser) {
    EnumCtx ctx;
    ctx.browser = browser.toLower();
    EnumWindows(enumBrowserProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

static bool isBrowserExe(const QString& exe) {
    return exe == QLatin1String("msedge.exe") || exe == QLatin1String("chrome.exe") ||
           exe == QLatin1String("firefox.exe") || exe == QLatin1String("brave.exe") ||
           exe == QLatin1String("msedgewebview2.exe");
}

static bool matchesDesktopWindow(const QString& titleRaw, const QString& exe, const QString& site) {
    const QString title = QString(titleRaw).remove(QChar(0x200b)).trimmed().toLower();
    if (isBrowserExe(exe))
        return false;
    // 进程名匹配即可（豆包等窗口标题可能为空或乱码）
    if (SiteCatalog::desktopProcessNames(site).contains(exe, Qt::CaseInsensitive))
        return true;
    if (title.isEmpty())
        return false;
    for (const QString& h : SiteCatalog::desktopTitleHints(site)) {
        if (title.contains(h))
            return true;
    }
    return false;
}

static BOOL CALLBACK enumDesktopProc(HWND hwnd, LPARAM lParam) {
    // Win32 EnumWindows 回调经 LPARAM 传上下文指针，为系统约定用法
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    if (!isTopLevelVisible(hwnd))
        return TRUE;
    const QString title = windowTitle(hwnd);
    const QString exe = processName(hwnd);
    if (!matchesDesktopWindow(title, exe, ctx->site))
        return TRUE;

    RECT r{};
    GetWindowRect(hwnd, &r);
    const LONG w = r.right - r.left;
    const LONG h = r.bottom - r.top;
    if (w < 200 || h < 120)
        return TRUE;
    // 最小化窗口常在 (-32000,-32000)，面积降权，优先选屏上主窗
    LONG area = w * h;
    if (r.left <= -10000 || r.top <= -10000)
        area /= 100;
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->found = hwnd;
    }
    return TRUE;
}

HWND findDesktopWindow(const QString& site) {
    EnumCtx ctx;
    ctx.site = site.toLower();
    EnumWindows(enumDesktopProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found) {
        if (IsIconic(ctx.found) || [&]() {
                RECT r{};
                GetWindowRect(ctx.found, &r);
                return r.left <= -10000;
            }()) {
            ShowWindow(ctx.found, SW_RESTORE);
            sleepMs(400);
        }
    }
    return ctx.found;
}

static QString localAppData() {
    QString env = qEnvironmentVariable("LOCALAPPDATA");
    if (!env.isEmpty())
        return env;
    return QDir::homePath() + QStringLiteral("/AppData/Local");
}

static QString fromAppPaths(const QString& exeName) {
    for (const QString& hive :
         {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        QSettings reg(
            hive + QStringLiteral("\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") +
                exeName,
            QSettings::NativeFormat);
        QString path = reg.value(QStringLiteral(".")).toString().trimmed();
        if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
            path = path.mid(1, path.size() - 2);
        path = QDir::fromNativeSeparators(path);
        if (!path.isEmpty() && QFileInfo::exists(path))
            return path;
    }
    return {};
}

QString findBrowserExe(const QString& browser) {
    const QString exeName = browser == QLatin1String("chrome")    ? QStringLiteral("chrome.exe")
                            : browser == QLatin1String("firefox") ? QStringLiteral("firefox.exe")
                                                                  : QStringLiteral("msedge.exe");
    return fromAppPaths(exeName);
}

static QString exeFromCommandString(const QString& cmd) {
    QString s = cmd.trimmed();
    if (s.isEmpty())
        return {};
    if (s.startsWith(QLatin1Char('"'))) {
        const qsizetype end = s.indexOf(QLatin1Char('"'), 1);
        if (end > 1)
            s = s.mid(1, end - 1);
    } else {
        const qsizetype sp = s.indexOf(QLatin1Char(' '));
        if (sp > 0)
            s = s.left(sp);
    }
    s = QDir::fromNativeSeparators(s);
    return QFileInfo::exists(s) ? s : QString();
}

static QString fromProtocolCommand(const QStringList& schemes) {
    for (const QString& scheme : schemes) {
        for (const QString& hive :
             {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
            QSettings reg(hive + QStringLiteral("\\Software\\Classes\\") + scheme +
                              QStringLiteral("\\shell\\open\\command"),
                          QSettings::NativeFormat);
            const QString path = exeFromCommandString(reg.value(QStringLiteral(".")).toString());
            if (!path.isEmpty())
                return path;
        }
    }
    return {};
}

static QString resolveShortcutTarget(const QString& lnkPath) {
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* sl = nullptr;
    QString target;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink,
                                   nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW,
                                   reinterpret_cast<void**>(&sl))) &&
        sl) {
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf))) && pf) {
            if (SUCCEEDED(pf->Load(reinterpret_cast<LPCWSTR>(lnkPath.utf16()), STGM_READ))) {
                wchar_t buf[MAX_PATH]{};
                if (SUCCEEDED(sl->GetPath(buf, MAX_PATH, nullptr, SLGP_UNCPRIORITY)))
                    target = QString::fromWCharArray(buf);
            }
            pf->Release();
        }
        sl->Release();
    }
    if (hrInit == S_OK)
        CoUninitialize();
    target = QDir::fromNativeSeparators(target);
    return QFileInfo::exists(target) ? target : QString();
}

static bool protocolRegistered(const QStringList& schemes) {
    for (const QString& scheme : schemes) {
        for (const QString& hive :
             {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
            QSettings reg(hive + QStringLiteral("\\Software\\Classes\\") + scheme,
                          QSettings::NativeFormat);
            if (!reg.childGroups().isEmpty() ||
                !reg.value(QStringLiteral(".")).toString().isEmpty())
                return true;
            QSettings cmd(hive + QStringLiteral("\\Software\\Classes\\") + scheme +
                              QStringLiteral("\\shell\\open\\command"),
                          QSettings::NativeFormat);
            if (!cmd.value(QStringLiteral(".")).toString().isEmpty())
                return true;
        }
    }
    return false;
}

static QString fromStartMenuShortcut(const QStringList& nameHints) {
    QStringList roots;
    roots << QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    roots << qEnvironmentVariable("ProgramData") +
                 QStringLiteral("/Microsoft/Windows/Start Menu/Programs");
    roots << QDir::homePath() +
                 QStringLiteral("/AppData/Roaming/Microsoft/Windows/Start Menu/Programs");
    for (const QString& root : roots) {
        if (root.isEmpty() || !QDir(root).exists())
            continue;
        QDirIterator it(
            root, QStringList{QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString base = it.fileInfo().completeBaseName().toLower();
            bool hit = false;
            for (const QString& h : nameHints) {
                if (base.contains(h.toLower())) {
                    hit = true;
                    break;
                }
            }
            if (!hit)
                continue;
            const QString target = resolveShortcutTarget(it.filePath());
            if (!target.isEmpty())
                return target;
        }
    }
    return {};
}

QString findDesktopAumid(const QString& site) {
    const QString root = localAppData() + QStringLiteral("/Packages");
    QDir dir(root);
    if (!dir.exists())
        return {};
    const QStringList prefixes = SiteCatalog::desktopPackagePrefixes(site);
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        const QString name = fi.fileName();
        for (const QString& p : prefixes) {
            if (name.startsWith(p))
                return name + QStringLiteral("!App");
        }
    }
    return {};
}

QString findDesktopExe(const QString& site) {
    const QString s = SiteCatalog::normalizeSite(site);
    for (const QString& name : SiteCatalog::desktopProcessNames(s)) {
        const QString path = fromAppPaths(name);
        if (!path.isEmpty())
            return path;
    }

    // 全站统一：协议命令 → 开始菜单快捷方式（含自定义盘符安装）
    if (const QString p = fromProtocolCommand(SiteCatalog::desktopProtocols(s)); !p.isEmpty())
        return p;
    if (const QString p = fromStartMenuShortcut(SiteCatalog::desktopShortcutHints(s)); !p.isEmpty())
        return p;

    const QString local = localAppData();
    const QString windowsApps = local + QStringLiteral("/Microsoft/WindowsApps");
    for (const QString& name : SiteCatalog::desktopProcessNames(s)) {
        const QString path = windowsApps + QLatin1Char('/') + name;
        if (QFileInfo::exists(path))
            return path;
    }

    QStringList patterns;
    if (s == QLatin1String("chatgpt")) {
        patterns << QStringLiteral("ChatGPT/ChatGPT.exe")
                 << QStringLiteral("Programs/ChatGPT/ChatGPT.exe");
    } else if (s == QLatin1String("claude")) {
        patterns << QStringLiteral("AnthropicClaude/claude.exe")
                 << QStringLiteral("Programs/Claude/Claude.exe");
    } else if (s == QLatin1String("gemini")) {
        patterns << QStringLiteral("Programs/Gemini/Gemini.exe")
                 << QStringLiteral("Google/Gemini/Gemini.exe");
    } else if (s == QLatin1String("doubao")) {
        patterns << QStringLiteral("Doubao/Application/Doubao.exe")
                 << QStringLiteral("Doubao/Doubao.exe")
                 << QStringLiteral("Programs/Doubao/Doubao.exe");
        // 常见自定义盘符安装
        for (const QString& root : {QStringLiteral("C:/Doubao/Doubao.exe"),
                                    QStringLiteral("D:/Doubao/Doubao.exe"),
                                    QStringLiteral("E:/Doubao/Doubao.exe")}) {
            if (QFileInfo::exists(root))
                return root;
        }
    } else if (s == QLatin1String("qwen")) {
        patterns << QStringLiteral("Programs/Qianwen/Qianwen.exe")
                 << QStringLiteral("Programs/Qianwen/Launcher.exe")
                 << QStringLiteral("Programs/QwenWorkCN/QwenWorkCN.exe")
                 << QStringLiteral("Programs/QwenWorkCN/Launcher.exe")
                 << QStringLiteral("tongyi/tongyi.exe");
    } else if (s == QLatin1String("deepseek")) {
        patterns << QStringLiteral("Programs/DeepSeek/DeepSeek.exe")
                 << QStringLiteral("Programs/DeepSeek Chat/DeepSeek Chat.exe")
                 << QStringLiteral("DeepSeek/DeepSeek.exe");
    }
    for (const QString& rel : patterns) {
        const QString path = local + QLatin1Char('/') + rel;
        if (QFileInfo::exists(path))
            return path;
    }

    // 全站：按进程名在常见根目录递归搜（含版本号目录）
    {
        QStringList roots;
        if (s == QLatin1String("claude"))
            roots << local + QStringLiteral("/AnthropicClaude");
        else if (s == QLatin1String("doubao"))
            roots << local + QStringLiteral("/Doubao");
        else if (s == QLatin1String("qwen"))
            roots << local + QStringLiteral("/Programs/Qianwen")
                  << local + QStringLiteral("/Programs/QwenWorkCN");
        else if (s == QLatin1String("deepseek"))
            roots << local + QStringLiteral("/Programs/DeepSeek");
        else if (s == QLatin1String("gemini"))
            roots << local + QStringLiteral("/Programs/Gemini")
                  << local + QStringLiteral("/Google/Gemini");
        else if (s == QLatin1String("chatgpt"))
            roots << local + QStringLiteral("/ChatGPT")
                  << local + QStringLiteral("/Programs/ChatGPT");

        for (const QString& root : roots) {
            if (!QDir(root).exists())
                continue;
            QDirIterator it(root,
                            SiteCatalog::desktopProcessNames(s),
                            QDir::Files,
                            QDirIterator::Subdirectories);
            QString best;
            while (it.hasNext()) {
                it.next();
                // 跳过明显非主程序的工具链目录
                const QString fp = it.filePath();
                if (fp.contains(QLatin1String("sandbox"), Qt::CaseInsensitive) ||
                    fp.contains(QLatin1String("repair"), Qt::CaseInsensitive))
                    continue;
                best = fp;
            }
            if (!best.isEmpty())
                return best;
        }
    }

    // 已在运行：从进程映像路径取 exe（Store 应用常见）
    if (HWND hwnd = findDesktopWindow(s)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            QString full;
            if (QueryFullProcessImageNameW(h, 0, path, &size))
                full = QString::fromWCharArray(path);
            CloseHandle(h);
            if (!full.isEmpty() && QFileInfo::exists(full))
                return full;
        }
    }
    return {};
}

namespace {
QMutex& desktopCacheMutex() {
    static QMutex m;
    return m;
}
QHash<QString, QPair<bool, qint64>>& desktopCache() {
    static QHash<QString, QPair<bool, qint64>> c;
    return c;
}
} // namespace

void invalidateDesktopCache() {
    QMutexLocker lock(&desktopCacheMutex());
    desktopCache().clear();
}

bool isDesktopInstalled(const QString& site) {
    const QString s = SiteCatalog::normalizeSite(site);
    constexpr qint64 kTtlMs = 3000;
    const qint64 now = static_cast<qint64>(GetTickCount64());

    {
        QMutexLocker lock(&desktopCacheMutex());
        const auto it = desktopCache().constFind(s);
        if (it != desktopCache().cend() && (now - it->second) < kTtlMs)
            return it->first;
    }

    bool installed = !findDesktopExe(s).isEmpty() || !findDesktopAumid(s).isEmpty() ||
                     findDesktopWindow(s) != nullptr;
    if (!installed) {
        const QStringList protocols = SiteCatalog::desktopProtocols(s);
        if (!protocols.isEmpty())
            installed = protocolRegistered(protocols);
    }

    {
        QMutexLocker lock(&desktopCacheMutex());
        desktopCache().insert(s, qMakePair(installed, now));
    }
    return installed;
}

bool launchBrowser(const QString& browser, const QString& url) {
    const QString exe = findBrowserExe(browser);
    if (!exe.isEmpty()) {
        // 直接交给浏览器打开 URL，不在地址栏逐字输入
        const QString b = browser.toLower();
        QString args;
        if (b == QLatin1String("firefox"))
            args = QStringLiteral("-new-tab \"%1\"").arg(url);
        else
            args = QStringLiteral("--new-tab \"%1\"").arg(url);
        return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,
                                                        L"open",
                                                        reinterpret_cast<LPCWSTR>(exe.utf16()),
                                                        reinterpret_cast<LPCWSTR>(args.utf16()),
                                                        nullptr,
                                                        SW_SHOWNORMAL)) > 32;
    }
    return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,
                                                    L"open",
                                                    reinterpret_cast<LPCWSTR>(url.utf16()),
                                                    nullptr,
                                                    nullptr,
                                                    SW_SHOWNORMAL)) > 32;
}

bool launchDesktop(const QString& site) {
    const QString s = SiteCatalog::normalizeSite(site);
    const QString exe = findDesktopExe(s);
    if (!exe.isEmpty()) {
        const auto rc =
            reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,
                                                     L"open",
                                                     reinterpret_cast<LPCWSTR>(exe.utf16()),
                                                     nullptr,
                                                     nullptr,
                                                     SW_SHOWNORMAL));
        if (rc > 32)
            return true;
    }
    const QString aumid = findDesktopAumid(s);
    if (!aumid.isEmpty()) {
        const QString explorer = QStringLiteral("explorer.exe");
        const QString args = QStringLiteral("shell:AppsFolder\\%1").arg(aumid);
        if (reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,
                                                     L"open",
                                                     reinterpret_cast<LPCWSTR>(explorer.utf16()),
                                                     reinterpret_cast<LPCWSTR>(args.utf16()),
                                                     nullptr,
                                                     SW_SHOWNORMAL)) > 32)
            return true;
    }
    // 全站：已注册的自定义协议唤起
    for (const QString& scheme : SiteCatalog::desktopProtocols(s)) {
        const QString uri = scheme + QLatin1Char(':');
        if (reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,
                                                     L"open",
                                                     reinterpret_cast<LPCWSTR>(uri.utf16()),
                                                     nullptr,
                                                     nullptr,
                                                     SW_SHOWNORMAL)) > 32)
            return true;
    }
    return false;
}

#endif

} // namespace WinInput
