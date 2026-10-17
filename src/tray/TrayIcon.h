#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;
class QAction;

class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);
    void retranslate(const QString &hotkeyLabel);
    void notify(const QString &message);

signals:
    void captureRequested();
    void settingsRequested();
    void quitRequested();

private:
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_capture = nullptr;
    QAction *m_settings = nullptr;
    QAction *m_quit = nullptr;
};
