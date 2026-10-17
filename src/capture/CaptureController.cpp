#include "capture/CaptureController.h"

#include "capture/CaptureOverlay.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

CaptureController::CaptureController(QObject *parent)
    : QObject(parent)
{
}

void CaptureController::setSettings(const Settings &settings)
{
    m_settings = settings;
}

void CaptureController::startCapture()
{
    if (m_overlay)
        return;

    QRect geo;
    for (QScreen *screen : QGuiApplication::screens())
        geo = geo.united(screen->geometry());

    // 拼接各屏截图
    QPixmap shot(geo.size());
    shot.fill(Qt::black);
    {
        QPainter p(&shot);
        for (QScreen *screen : QGuiApplication::screens()) {
            const QPixmap part = screen->grabWindow(0);
            p.drawPixmap(screen->geometry().topLeft() - geo.topLeft(), part);
        }
    }

    const bool fixed = m_settings.selectMode == QLatin1String("fixed");
    m_overlay = new CaptureOverlay(shot, fixed, m_settings.fixedWidth, m_settings.fixedHeight);
    connect(m_overlay, &CaptureOverlay::finished, this, [this](const QPixmap &image, bool sendAi) {
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

void CaptureController::onOverlayFinished(const QPixmap &image, bool sendAi)
{
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (image.isNull()) {
        emit captureFinished(false);
        return;
    }
    QApplication::clipboard()->setPixmap(image);
    if (sendAi) {
        emit aiInquireRequested(image);
        // busy 由 Application::onAiInquire / AiSender::finished 管理
    } else {
        emit captureFinished(true);
    }
}
