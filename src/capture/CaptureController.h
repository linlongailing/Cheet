#pragma once

#include "settings/Settings.h"

#include <QObject>
#include <QPixmap>

class CaptureOverlay;

class CaptureController : public QObject
{
    Q_OBJECT
public:
    explicit CaptureController(QObject *parent = nullptr);

    void setSettings(const Settings &settings);
    void startCapture();

signals:
    void captureFinished(bool copied);
    void aiInquireRequested(const QPixmap &image);

private:
    void onOverlayFinished(const QPixmap &image, bool sendAi);

    Settings m_settings;
    CaptureOverlay *m_overlay = nullptr;
};
