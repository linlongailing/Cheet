#include "tray/TrayIcon.h"

#include "i18n/I18n.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon(QStringLiteral(":/qt/qml/CheetahClip/resources/icons/icon.png")));
    m_tray->setToolTip(QStringLiteral("cheetahClip"));

    m_menu = new QMenu;
    m_capture = m_menu->addAction(QString());
    m_settings = m_menu->addAction(QString());
    m_menu->addSeparator();
    m_quit = m_menu->addAction(QString());

    connect(m_capture, &QAction::triggered, this, &TrayIcon::captureRequested);
    connect(m_settings, &QAction::triggered, this, &TrayIcon::settingsRequested);
    connect(m_quit, &QAction::triggered, this, &TrayIcon::quitRequested);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit settingsRequested();
    });

    m_tray->setContextMenu(m_menu);
    retranslate(QStringLiteral("Alt + A"));
    m_tray->show();
}

void TrayIcon::retranslate(const QString &hotkeyLabel)
{
    auto &i18n = I18n::instance();
    m_capture->setText(i18n.tr(QStringLiteral("tray_capture"), {{QStringLiteral("hotkey"), hotkeyLabel}}));
    m_settings->setText(i18n.tr(QStringLiteral("tray_settings")));
    m_quit->setText(i18n.tr(QStringLiteral("tray_quit")));
}

void TrayIcon::notify(const QString &message)
{
    if (m_tray)
        m_tray->showMessage(QStringLiteral("cheetahClip"), message, QSystemTrayIcon::Information, 3500);
}
