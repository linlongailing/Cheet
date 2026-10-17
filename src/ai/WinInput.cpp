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
#include <QThread>

#ifdef Q_OS_WIN
#include <shellapi.h>
#endif

namespace WinInput {

#ifdef Q_OS_WIN

void sleepMs(int ms)
{
    QThread::msleep(static_cast<unsigned long>(ms));
}

bool setForeground(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    AllowSetForegroundWindow(ASFW_ANY);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    return GetForegroundWindow() == hwnd || IsWindow(hwnd);
}

static void sendVk(WORD vk, bool up)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

void sendCtrlKey(WORD vk)
{
    sendVk(VK_CONTROL, false);
    sleepMs(20);
    sendVk(vk, false);
    sleepMs(20);
    sendVk(vk, true);
    sleepMs(20);
    sendVk(VK_CONTROL, true);
    sleepMs(40);
}

void sendCtrlShiftKey(WORD vk)
{
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

void sendKey(WORD vk)
{
    sendVk(vk, false);
    sleepMs(20);
    sendVk(vk, true);
    sleepMs(40);
}

void sendEnter()
{
    sendKey(VK_RETURN);
}

void sendText(const QString &text)
{
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
};

static QString windowTitle(HWND hwnd)
{
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return QString::fromWCharArray(buf);
}

static QString className(HWND hwnd)
{
    wchar_t buf[256]{};
    GetClassNameW(hwnd, buf, 256);
    return QString::fromWCharArray(buf);
}

static QString processName(HWND hwnd)
{
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

static bool isTopLevelVisible(HWND hwnd)
{
    return IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER);
}

static BOOL CALLBACK enumBrowserProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<EnumCtx *>(lParam);
    if (!isTopLevelVisible(hwnd))
        return TRUE;
    const QString title = windowTitle(hwnd).toLower();
    const QString cls = className(hwnd);
    const QString exe = processName(hwnd);
    if (title.isEmpty())
        return TRUE;

    if (ctx->browser == QLatin1String("firefox")) {
        if (cls == QLatin1String("MozillaWindowClass")
            && (title.contains(QLatin1String("firefox")) || exe == QLatin1String("firefox.exe"))) {
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
        if (title.contains(QLatin1String("google chrome")) || title.contains(QLatin1String("brave")))
            return TRUE;
        if (title.contains(QLatin1String("edge")) || exe == QLatin1String("msedge.exe")) {
            ctx->found = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND findBrowserWindow(const QString &browser)
{
    EnumCtx ctx;
    ctx.browser = browser.toLower();
    EnumWindows(enumBrowserProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

static bool isBrowserExe(const QString &exe)
{
    return exe == QLatin1String("msedge.exe") || exe == QLatin1String("chrome.exe")
        || exe == QLatin1String("firefox.exe") || exe == QLatin1String("brave.exe")
        || exe == QLatin1String("msedgewebview2.exe");
}

static bool matchesDesktopWindow(const QString &titleRaw, const QString &exe, const QString &site)
{
    const QString title = QString(titleRaw).remove(QChar(0x200b)).trimmed().toLower();
    if (title.isEmpty())
        return false;
    if (isBrowserExe(exe))
        return false;
    if (SiteCatalog::desktopProcessNames(site).contains(exe))
        return true;
    for (const QString &h : SiteCatalog::desktopTitleHints(site)) {
        if (title.contains(h))
            return true;
    }
    return false;
}

static BOOL CALLBACK enumDesktopProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<EnumCtx *>(lParam);
    if (!isTopLevelVisible(hwnd))
        return TRUE;
    const QString title = windowTitle(hwnd);
    const QString exe = processName(hwnd);
    if (matchesDesktopWindow(title, exe, ctx->site)) {
        ctx->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND findDesktopWindow(const QString &site)
{
    EnumCtx ctx;
    ctx.site = site.toLower();
    EnumWindows(enumDesktopProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

static QString localAppData()
{
    const QString env = qEnvironmentVariable("LOCALAPPDATA");
    if (!env.isEmpty())
        return env;
    return QDir::homePath() + QStringLiteral("/AppData/Local");
}

static QString fromAppPaths(const QString &exeName)
{
    for (const QString &hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        QSettings reg(hive + QStringLiteral("\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + exeName,
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

QString findBrowserExe(const QString &browser)
{
    const QString exeName = browser == QLatin1String("chrome")   ? QStringLiteral("chrome.exe")
                            : browser == QLatin1String("firefox") ? QStringLiteral("firefox.exe")
                                                                  : QStringLiteral("msedge.exe");
    return fromAppPaths(exeName);
}

QString findDesktopAumid(const QString &site)
{
    const QString root = localAppData() + QStringLiteral("/Packages");
    QDir dir(root);
    if (!dir.exists())
        return {};
    const QStringList prefixes = SiteCatalog::desktopPackagePrefixes(site);
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        for (const QString &p : prefixes) {
            if (name.startsWith(p))
                return name + QStringLiteral("!App");
        }
    }
    return {};
}

QString findDesktopExe(const QString &site)
{
    const QString s = SiteCatalog::normalizeSite(site);
    for (const QString &name : SiteCatalog::desktopProcessNames(s)) {
        const QString path = fromAppPaths(name);
        if (!path.isEmpty())
            return path;
    }

    const QString local = localAppData();
    const QString windowsApps = local + QStringLiteral("/Microsoft/WindowsApps");
    for (const QString &name : SiteCatalog::desktopProcessNames(s)) {
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
    }
    for (const QString &rel : patterns) {
        const QString path = local + QLatin1Char('/') + rel;
        if (QFileInfo::exists(path))
            return path;
    }

    // Claude 常见带版本号目录
    if (s == QLatin1String("claude")) {
        QDirIterator it(local + QStringLiteral("/AnthropicClaude"), QStringList{QStringLiteral("claude.exe")},
                        QDir::Files, QDirIterator::Subdirectories);
        QString best;
        while (it.hasNext()) {
            it.next();
            best = it.filePath();
        }
        if (!best.isEmpty())
            return best;
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

bool isDesktopInstalled(const QString &site)
{
    const QString s = SiteCatalog::normalizeSite(site);
    // 短 TTL 缓存，避免设置页频繁扫 Packages / EnumWindows
    static QMutex mutex;
    static QHash<QString, QPair<bool, qint64>> cache;
    constexpr qint64 kTtlMs = 3000;
    const qint64 now = static_cast<qint64>(GetTickCount64());

    {
        QMutexLocker lock(&mutex);
        const auto it = cache.constFind(s);
        if (it != cache.cend() && (now - it->second) < kTtlMs)
            return it->first;
    }

    const bool installed = !findDesktopExe(s).isEmpty() || !findDesktopAumid(s).isEmpty()
        || findDesktopWindow(s) != nullptr;

    {
        QMutexLocker lock(&mutex);
        cache.insert(s, qMakePair(installed, now));
    }
    return installed;
}

bool launchBrowser(const QString &browser, const QString &url)
{
    const QString exe = findBrowserExe(browser);
    if (!exe.isEmpty()) {
        const QString args = QStringLiteral("\"%1\"").arg(url);
        return reinterpret_cast<intptr_t>(ShellExecuteW(
                   nullptr, L"open", reinterpret_cast<LPCWSTR>(exe.utf16()),
                   reinterpret_cast<LPCWSTR>(args.utf16()), nullptr, SW_SHOWNORMAL))
            > 32;
    }
    return reinterpret_cast<intptr_t>(ShellExecuteW(
               nullptr, L"open", reinterpret_cast<LPCWSTR>(url.utf16()), nullptr, nullptr, SW_SHOWNORMAL))
        > 32;
}

bool launchDesktop(const QString &site)
{
    const QString exe = findDesktopExe(site);
    if (!exe.isEmpty()) {
        const auto rc = reinterpret_cast<intptr_t>(ShellExecuteW(
            nullptr, L"open", reinterpret_cast<LPCWSTR>(exe.utf16()), nullptr, nullptr, SW_SHOWNORMAL));
        if (rc > 32)
            return true;
    }
    const QString aumid = findDesktopAumid(site);
    if (aumid.isEmpty())
        return false;
    // shell:AppsFolder\<AUMID>
    const QString explorer = QStringLiteral("explorer.exe");
    const QString args = QStringLiteral("shell:AppsFolder\\%1").arg(aumid);
    return reinterpret_cast<intptr_t>(ShellExecuteW(
               nullptr, L"open", reinterpret_cast<LPCWSTR>(explorer.utf16()),
               reinterpret_cast<LPCWSTR>(args.utf16()), nullptr, SW_SHOWNORMAL))
        > 32;
}

#endif

} // namespace WinInput
