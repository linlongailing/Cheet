#pragma once

#include "settings/Settings.h"

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QtGlobal>

/** 打开目标（桌面优先）→ 粘贴图片 → 监测上传 → 填提示词 → Enter 发送。 */
class AiSender : public QObject
{
    Q_OBJECT
public:
    explicit AiSender(QObject *parent = nullptr);

    void sendAsync(const Settings &settings, const QPixmap &image);

signals:
    void notify(const QString &message);
    void finished(bool ok);

private:
    bool sendSync(const Settings &settings, const QPixmap &image);
    /** desktopMode=true 只走桌面端；窗口中途关闭立即返回 false。 */
    bool pastePromptAndSend(const Settings &settings, quintptr hwndValue, bool desktopMode,
                            const QPixmap &image);
    QString saveTempImage(const QPixmap &image) const;

    void emitNotify(const QString &msg);
};
