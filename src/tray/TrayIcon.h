#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QWidget;
class QPushButton;

/** 托盘图标 + 白底圆角简洁右键菜单。 */
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject* parent = nullptr);
    ~TrayIcon() override;
    void retranslate(const QString& hotkeyLabel);
    void notify(const QString& message);

signals:
    void captureRequested();
    void settingsRequested();
    void quitRequested();

private:
    void showMenuAtCursor();
    void hideMenu();

    QSystemTrayIcon* m_tray = nullptr;
    QWidget* m_menu = nullptr;
    QPushButton* m_captureBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    QPushButton* m_quitBtn = nullptr;
    QString m_hotkeyLabel;
};
