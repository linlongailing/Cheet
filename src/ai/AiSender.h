#pragma once

#include "settings/Settings.h"

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QtGlobal>

#include <atomic>

/** 打开目标（桌面优先）→ 粘贴图片 → 监测上传 → 填提示词 → Enter 发送。 */
class AiSender : public QObject {
    Q_OBJECT
public:
    explicit AiSender(QObject* parent = nullptr);

    void sendAsync(const Settings& settings, const QPixmap& image);

signals:
    void notify(const QString& message);
    void finished(bool ok);

private:
    bool sendSync(const Settings& settings, const QPixmap& image);
    /** desktopMode=true 只走桌面端；窗口中途关闭立即返回 false。 */
    bool pastePromptAndSend(const Settings& settings,
                            quintptr hwndValue,
                            bool desktopMode,
                            const QPixmap& image);

    void emitNotify(const QString& msg);

    /** 同一时刻只允许一个发送任务在跑，防止并发任务抢剪贴板/前台窗口/模拟输入。 */
    std::atomic_bool m_sending{false};
    /** 本次发送是否因「已上传图片被用户删除」而中止（仅工作线程读写）：静默结束，不弹提示。 */
    bool m_imageRemovedAbort = false;
};
