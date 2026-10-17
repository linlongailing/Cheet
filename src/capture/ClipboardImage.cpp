#include "capture/ClipboardImage.h"

#include <QBuffer>
#include <QClipboard>
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
#include <windows.h>
#include <shlobj.h>
#endif

namespace ClipboardImage {
namespace {

QString saveTempPng(const QImage &img)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty() || img.isNull())
        return {};
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/Cheet_clipboard.png");
    if (!img.save(path, "PNG"))
        return {};
    return QDir::toNativeSeparators(path);
}

#ifdef Q_OS_WIN

bool setClipboardWin(const QImage &img, const QString &filePath)
{
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

    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();

    auto allocMoveable = [](const void *data, size_t size) -> HGLOBAL {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!h)
            return nullptr;
        void *p = GlobalLock(h);
        if (!p) {
            GlobalFree(h);
            return nullptr;
        }
        memcpy(p, data, size);
        GlobalUnlock(h);
        return h;
    };

    bool ok = false;

    // PNG（Chrome / Edge 网页粘贴）
    if (!png.isEmpty()) {
        const UINT cfPng = RegisterClipboardFormatW(L"PNG");
        if (cfPng) {
            if (HGLOBAL h = allocMoveable(png.constData(), static_cast<size_t>(png.size()))) {
                if (SetClipboardData(cfPng, h))
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

    // CF_HDROP：桌面 / 文件夹「粘贴」出文件
    if (!filePath.isEmpty()) {
        const std::wstring wpath = filePath.toStdWString();
        const size_t pathBytes = (wpath.size() + 1) * sizeof(wchar_t);
        const size_t total = sizeof(DROPFILES) + pathBytes + sizeof(wchar_t); // 双 \0 结尾
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, total)) {
            auto *df = static_cast<DROPFILES *>(GlobalLock(h));
            if (df) {
                memset(df, 0, total);
                df->pFiles = sizeof(DROPFILES);
                df->fWide = TRUE;
                memcpy(reinterpret_cast<char *>(df) + sizeof(DROPFILES), wpath.c_str(), pathBytes);
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
    return ok;
}

#endif // Q_OS_WIN

} // namespace

QString copyForPaste(const QPixmap &image)
{
    if (image.isNull())
        return {};

    const QImage img = image.toImage();
    const QString path = saveTempPng(img);

#ifdef Q_OS_WIN
    if (setClipboardWin(img, path))
        return path;
#endif

    // 非 Windows 或 Win32 失败时的 Qt 回退
    auto *mime = new QMimeData;
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

} // namespace ClipboardImage
