#include "app/SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
    , m_shm(shmKey())
{
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(serverName());
    }
    if (m_shm.isAttached())
        m_shm.detach();
}

QString SingleInstance::serverName()
{
    return QStringLiteral("Cheet_SingleInstance_IPC");
}

QString SingleInstance::shmKey()
{
    return QStringLiteral("Cheet_SingleInstance_SHM");
}

bool SingleInstance::tryRunAsPrimary()
{
    // 尝试连接已有实例
    {
        QLocalSocket sock;
        sock.connectToServer(serverName());
        if (sock.waitForConnected(400)) {
            sock.write("activate\n");
            sock.flush();
            sock.waitForBytesWritten(400);
            sock.disconnectFromServer();
            return false;
        }
    }

    // 清理崩溃残留的本地套接字名
    QLocalServer::removeServer(serverName());

    // 共享内存作为进程锁；若残留则先卸再创
    if (!m_shm.create(1)) {
        if (m_shm.error() == QSharedMemory::AlreadyExists) {
            if (m_shm.attach())
                m_shm.detach();
            if (!m_shm.create(1)) {
                // 仍失败：再试一次 IPC（可能竞态）
                QLocalSocket sock;
                sock.connectToServer(serverName());
                if (sock.waitForConnected(400)) {
                    sock.write("activate\n");
                    sock.flush();
                    sock.waitForBytesWritten(400);
                    return false;
                }
                // 强制占用
                QLocalServer::removeServer(serverName());
                if (!m_shm.create(1) && !m_shm.attach())
                    return true; // 退化：允许启动，避免用户完全打不开
            }
        }
    }

    m_server = new QLocalServer(this);
    if (!m_server->listen(serverName())) {
        QLocalServer::removeServer(serverName());
        m_server->listen(serverName());
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    return true;
}

void SingleInstance::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *client = m_server->nextPendingConnection();
        if (!client)
            continue;
        connect(client, &QLocalSocket::readyRead, this, [this, client]() {
            const QByteArray data = client->readAll();
            if (data.contains("activate"))
                emit activateRequested();
            client->disconnectFromServer();
            client->deleteLater();
        });
        connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
    }
}
