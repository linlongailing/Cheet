#pragma once

#include <QObject>
#include <QSharedMemory>

class QLocalServer;

/**
 * 单实例守卫：第二进程向已有实例发激活信号后立即退出。
 * 已有实例收到后打开设置窗口（托盘应用无常驻主窗）。
 */
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    /** @return true=本进程为主实例；false=已有实例并已请求激活 */
    bool tryRunAsPrimary();

signals:
    void activateRequested();

private:
    void onNewConnection();

    QSharedMemory m_shm;
    QLocalServer* m_server = nullptr;
    static QString serverName();
    static QString shmKey();
};
