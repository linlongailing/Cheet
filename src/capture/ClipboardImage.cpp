#include "capture/ClipboardImage.h"

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>

#include <cstring>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#include <shlobj.h>
#include <windows.h>
#endif

namespace ClipboardImage {
namespace {

QString saveTempPng(const QImage& img) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty() || img.isNull())
        return {};
    QDir().mkpath(dir);
    // 每次截图用时间戳文件名，粘贴到桌面/文件夹时不会互相覆盖
    const QString path =
        dir + QStringLiteral("/Cheet_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    if (!img.save(path, "PNG"))
        return {};
    return QDir::toNativeSeparators(path);
}

#ifdef Q_OS_WIN

/**
 * 剪贴板守护：截图写入后，若剪贴板被其他程序清成「空」，自动把截图放回；
 * 一旦剪贴板出现别的新内容（用户又复制了东西），立即停止守护、绝不覆盖。
 * 所有调用都在 GUI 线程（WM_CLIPBOARDUPDATE 也由 GUI 线程消息循环派发）。
 */
struct ClipGuard {
    HWND owner = nullptr; // 剪贴板所有者（隐藏消息窗口）+ 变化监听
    QImage image;
    QString path;
    bool includeHdrop = false;
    bool armed = false;
    bool suspended = false; // AI 发送流程期间暂停，剪贴板完全交给发送流程
    DWORD ourSeq = 0;       // 我们最后一次写入后的剪贴板序号，用于识别自己触发的通知
    ULONGLONG lastRestoreAt = 0;
    int burst = 0;
};

ClipGuard& clipGuard() {
    static ClipGuard g;
    return g;
}

// 补救间隔小于该值视为连续被清；连续多次说明有程序在反复清空，停止以免来回抢
constexpr ULONGLONG kBurstWindowMs = 1000;
constexpr int kMaxBurst = 5;

bool writeClipboard(const QImage& img, const QString& filePath, bool includeHdrop);

void disarmGuard() {
    ClipGuard& g = clipGuard();
    g.armed = false;
    g.image = QImage();
    g.path.clear();
}

void onClipboardUpdate() {
    ClipGuard& g = clipGuard();
    if (!g.armed || g.suspended)
        return;
    if (GetClipboardSequenceNumber() == g.ourSeq)
        return; // 自己写入触发的通知
    if (CountClipboardFormats() > 0) {
        disarmGuard(); // 用户复制了别的新内容：尊重新内容，停止守护
        return;
    }
    const ULONGLONG now = GetTickCount64();
    g.burst = (g.lastRestoreAt && now - g.lastRestoreAt < kBurstWindowMs) ? g.burst + 1 : 0;
    g.lastRestoreAt = now;
    if (g.burst >= kMaxBurst) {
        qWarning("ClipboardImage: clipboard keeps being emptied, stop guarding");
        disarmGuard();
        return;
    }
    qWarning("ClipboardImage: clipboard was emptied externally, restoring screenshot");
    writeClipboard(g.image, g.path, g.includeHdrop);
}

LRESULT CALLBACK clipOwnerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLIPBOARDUPDATE) {
        onClipboardUpdate();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/** 惰性创建隐藏消息窗口：作为剪贴板所有者，并注册剪贴板变化监听 */
HWND clipOwnerWindow() {
    ClipGuard& g = clipGuard();
    if (g.owner && IsWindow(g.owner))
        return g.owner;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = clipOwnerProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CheetClipboardOwner";
    RegisterClassW(&wc); // 重复注册失败无妨
    g.owner = CreateWindowExW(
        0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (g.owner)
        AddClipboardFormatListener(g.owner);
    return g.owner;
}

/** 打开剪贴板：带所有者窗口；被其他程序占用时短暂重试 */
bool openClipboardWithRetry(HWND owner) {
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(owner))
            return true;
        Sleep(15);
    }
    return false;
}

bool setClipboardWin(const QImage& img, const QString& filePath, bool includeHdrop) {
    if (!writeClipboard(img, filePath, includeHdrop))
        return false;
    ClipGuard& g = clipGuard();
    g.image = img;
    g.path = filePath;
    g.includeHdrop = includeHdrop;
    g.armed = true;
    g.lastRestoreAt = 0;
    g.burst = 0;
    return true;
}

bool writeClipboard(const QImage& img, const QString& filePath, bool includeHdrop) {
    QImage rgb = img.convertToFormat(QImage::Format_RGB32);
    if (rgb.isNull())
        return false;

    // BMP file bytes then strip 14-byte BITMAPFILEHEADER → CF_DIB payload
    QByteArray bmp;
    {
        QBuffer buf(&bmp);
        buf.open(QIODevice::WriteOnly);
        if (!rgb.save(&buf, "BMP") || bmp.size() <= 14)
            return false;
    }
    const QByteArray dib = bmp.mid(14);

    QByteArray png;
    {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        if (!rgb.save(&buf, "PNG"))
            png.clear();
    }

    // 必须带所有者窗口打开：OpenClipboard(nullptr) 会让剪贴板变成无主数据
    // （文档明确不支持），容易被其他程序/OLE 交接当成垃圾清掉
    if (!openClipboardWithRetry(clipOwnerWindow()))
        return false;
    EmptyClipboard();

    auto allocMoveable = [](const void* data, size_t size) -> HGLOBAL {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!h)
            return nullptr;
        void* p = GlobalLock(h);
        if (!p) {
            GlobalFree(h);
            return nullptr;
        }
        memcpy(p, data, size);
        GlobalUnlock(h);
        return h;
    };

    bool ok = false;

    // PNG（Chrome / Edge / 豆包网页粘贴）
    if (!png.isEmpty()) {
        for (const wchar_t* fmt : {L"PNG", L"image/png"}) {
            const UINT cf = RegisterClipboardFormatW(fmt);
            if (!cf)
                continue;
            if (HGLOBAL h = allocMoveable(png.constData(), static_cast<size_t>(png.size()))) {
                if (SetClipboardData(cf, h))
                    ok = true;
                else
                    GlobalFree(h);
            }
        }
    }

    // DIB（通用图片粘贴）
    if (HGLOBAL h = allocMoveable(dib.constData(), static_cast<size_t>(dib.size()))) {
        if (SetClipboardData(CF_DIB, h))
            ok = true;
        else
            GlobalFree(h);
    }

    // CF_HDROP：桌面 / 文件夹「粘贴」出文件（网页端勿带，避免豆包不走上图）
    if (includeHdrop && !filePath.isEmpty()) {
        const std::wstring wpath = filePath.toStdWString();
        const size_t pathBytes = (wpath.size() + 1) * sizeof(wchar_t);
        const size_t total = sizeof(DROPFILES) + pathBytes + sizeof(wchar_t); // 双 \0 结尾
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, total)) {
            auto* df = static_cast<DROPFILES*>(GlobalLock(h));
            if (df) {
                memset(df, 0, total);
                df->pFiles = sizeof(DROPFILES);
                df->fWide = TRUE;
                memcpy(reinterpret_cast<char*>(df) + sizeof(DROPFILES), wpath.c_str(), pathBytes);
                GlobalUnlock(h);
                if (SetClipboardData(CF_HDROP, h))
                    ok = true;
                else
                    GlobalFree(h);
            } else {
                GlobalFree(h);
            }
        }
    }

    CloseClipboard();
    clipGuard().ourSeq = GetClipboardSequenceNumber();
    return ok;
}

#endif // Q_OS_WIN

} // namespace

QString copyForPaste(const QPixmap& image) {
    if (image.isNull())
        return {};

    const QImage img = image.toImage();
    const QString path = saveTempPng(img);

#ifdef Q_OS_WIN
    if (setClipboardWin(img, path, true))
        return path;
#endif

    // 非 Windows 或 Win32 失败时的 Qt 回退
    auto* mime = new QMimeData;
    mime->setImageData(img);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    if (!png.isEmpty())
        mime->setData(QStringLiteral("image/png"), png);
    if (!path.isEmpty())
        mime->setUrls({QUrl::fromLocalFile(path)});
    QGuiApplication::clipboard()->setMimeData(mime);
    return path;
}

QString copyForWebPaste(const QPixmap& image) {
    if (image.isNull())
        return {};

    const QImage img = image.toImage();
    const QString path = saveTempPng(img);

#ifdef Q_OS_WIN
    if (setClipboardWin(img, path, false))
        return path;
#endif

    auto* mime = new QMimeData;
    mime->setImageData(img);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    if (!png.isEmpty())
        mime->setData(QStringLiteral("image/png"), png);
    QGuiApplication::clipboard()->setMimeData(mime);
    return path;
}

void setGuardSuspended(bool suspended) {
#ifdef Q_OS_WIN
    clipGuard().suspended = suspended;
#else
    Q_UNUSED(suspended);
#endif
}

} // namespace ClipboardImage
