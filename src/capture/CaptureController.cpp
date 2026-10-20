#include "capture/CaptureController.h"

#include "capture/CaptureOverlay.h"
#include "capture/ClipboardImage.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

CaptureController::CaptureController(QObject* parent) : QObject(parent) {}

void CaptureController::setSettings(const Settings& settings) {
    m_settings = settings;
}

void CaptureController::startCapture() {
    if (m_overlay)
        return;

    QRect geo;
    qreal dpr = 1.0;
    for (QScreen* screen : QGuiApplication::screens()) {
        geo = geo.united(screen->geometry());
        dpr = qMax(dpr, screen->devicePixelRatio());
    }
    if (geo.isEmpty()) {
        emit captureFinished(false);
        return;
    }

    // 按物理像素拼接，并设置 devicePixelRatio，避免 125%/150%/200% 缩成糊图
    const QSize pixelSize(qRound(geo.width() * dpr), qRound(geo.height() * dpr));
    QPixmap shot(pixelSize);
    shot.setDevicePixelRatio(dpr);
    shot.fill(Qt::black);
    {
        QPainter p(&shot);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (QScreen* screen : QGuiApplication::screens()) {
            QPixmap part = screen->grabWindow(0);
            if (part.isNull())
                continue;
            // grabWindow 一般为物理像素；补齐 DPR 以便按逻辑坐标绘制不丢清晰度
            if (part.devicePixelRatio() < 1.0)
                part.setDevicePixelRatio(1.0);
            const qreal partDpr = screen->devicePixelRatio();
            if (!qFuzzyCompare(part.devicePixelRatio(), partDpr))
                part.setDevicePixelRatio(partDpr);
            const QPoint topLeft = screen->geometry().topLeft() - geo.topLeft();
            p.drawPixmap(topLeft, part);
        }
    }

    const bool fixed = m_settings.selectMode == QLatin1String("fixed");
    m_overlay = new CaptureOverlay(shot, fixed, m_settings.fixedWidth, m_settings.fixedHeight);
    connect(m_overlay, &CaptureOverlay::finished, this, [this](const QPixmap& image, bool sendAi) {
        onOverlayFinished(image, sendAi);
    });
    connect(m_overlay, &CaptureOverlay::cancelled, this, [this]() {
        if (m_overlay) {
            m_overlay->deleteLater();
            m_overlay = nullptr;
        }
        emit captureFinished(false);
    });
    m_overlay->show();
    m_overlay->activateWindow();
    m_overlay->raise();
}

void CaptureController::onOverlayFinished(const QPixmap& image, bool sendAi) {
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (image.isNull()) {
        emit captureFinished(false);
        return;
    }
    // 图片 + 临时文件，支持粘贴到桌面/文件夹及其他应用
    ClipboardImage::copyForPaste(image);
    if (sendAi) {
        emit aiInquireRequested(image);
        // busy 由 Application::onAiInquire / AiSender::finished 管理
    } else {
        emit captureFinished(true);
    }
}
