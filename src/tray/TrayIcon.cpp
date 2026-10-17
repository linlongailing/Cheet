#include "tray/TrayIcon.h"

#include "i18n/I18n.h"

#include <QApplication>
#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

namespace {

constexpr int kMenuPad = 8;
constexpr int kShadowMargin = 20;

QPushButton *makeItem(QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setMinimumHeight(40);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon(QStringLiteral(":/qt/qml/CheetahClip/resources/icons/icon.png")));
    m_tray->setToolTip(QStringLiteral("cheetahClip"));

    // 自定义弹出菜单（不用原生 QMenu，以便圆角白底样式）
    m_menu = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::Popup | Qt::NoDropShadowWindowHint);
    m_menu->setAttribute(Qt::WA_TranslucentBackground);
    m_menu->setAttribute(Qt::WA_ShowWithoutActivating);

    auto *outer = new QVBoxLayout(m_menu);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);
    outer->setSpacing(0);

    auto *card = new QWidget(m_menu);
    card->setObjectName(QStringLiteral("trayMenuCard"));
    card->setStyleSheet(QStringLiteral(
        "QWidget#trayMenuCard {"
        "  background: #ffffff;"
        "  border-radius: 16px;"
        "}"
        "QPushButton {"
        "  border: none;"
        "  background: transparent;"
        "  color: #111111;"
        "  text-align: left;"
        "  padding: 10px 24px;"
        "  font-family: 'Microsoft YaHei UI';"
        "  font-size: 14px;"
        "  border-radius: 10px;"
        "}"
        "QPushButton:hover {"
        "  background: #f2f2f2;"
        "}"
        "QPushButton:pressed {"
        "  background: #e8e8e8;"
        "}"));

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 55));
    card->setGraphicsEffect(shadow);

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(kMenuPad, kMenuPad, kMenuPad, kMenuPad);
    lay->setSpacing(2);

    m_captureBtn = makeItem(card);
    m_settingsBtn = makeItem(card);
    m_quitBtn = makeItem(card);
    lay->addWidget(m_captureBtn);
    lay->addWidget(m_settingsBtn);
    lay->addWidget(m_quitBtn);
    outer->addWidget(card);

    connect(m_captureBtn, &QPushButton::clicked, this, [this]() {
        hideMenu();
        emit captureRequested();
    });
    connect(m_settingsBtn, &QPushButton::clicked, this, [this]() {
        hideMenu();
        emit settingsRequested();
    });
    connect(m_quitBtn, &QPushButton::clicked, this, [this]() {
        hideMenu();
        emit quitRequested();
    });

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Context) {
            showMenuAtCursor();
            return;
        }
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit settingsRequested();
    });

    retranslate(QStringLiteral("Alt + A"));
    m_tray->show();
}

TrayIcon::~TrayIcon()
{
    delete m_menu;
    m_menu = nullptr;
}

void TrayIcon::retranslate(const QString &hotkeyLabel)
{
    m_hotkeyLabel = hotkeyLabel;
    auto &i18n = I18n::instance();
    if (m_captureBtn)
        m_captureBtn->setText(
            i18n.tr(QStringLiteral("tray_capture"), {{QStringLiteral("hotkey"), hotkeyLabel}}));
    if (m_settingsBtn)
        m_settingsBtn->setText(i18n.tr(QStringLiteral("tray_settings")));
    if (m_quitBtn)
        m_quitBtn->setText(i18n.tr(QStringLiteral("tray_quit")));
}

void TrayIcon::notify(const QString &message)
{
    if (m_tray)
        m_tray->showMessage(QStringLiteral("cheetahClip"), message, QSystemTrayIcon::Information, 3500);
}

void TrayIcon::hideMenu()
{
    if (m_menu)
        m_menu->hide();
}

void TrayIcon::showMenuAtCursor()
{
    if (!m_menu)
        return;

    retranslate(m_hotkeyLabel);
    m_menu->adjustSize();

    QPoint pos;
    const QRect icon = m_tray ? m_tray->geometry() : QRect();
    if (icon.isValid() && icon.width() > 0 && icon.height() > 0) {
        // 紧贴图标正上方：卡片底边 = 图标顶边；左缘对齐图标左缘，菜单向右展开（避免右对齐显得偏左）
        // 窗口含阴影边距：卡片左 = pos.x()+kShadowMargin，卡片底 = pos.y()+height-kShadowMargin
        pos.setX(icon.left() - kShadowMargin);
        pos.setY(icon.top() - m_menu->height() + kShadowMargin);
    } else {
        const QPoint c = QCursor::pos();
        pos.setX(c.x() - kShadowMargin);
        pos.setY(c.y() - m_menu->height() + kShadowMargin);
    }

    const QPoint anchor = icon.isValid() ? icon.center() : QCursor::pos();
    if (QScreen *screen = QGuiApplication::screenAt(anchor)) {
        const QRect ag = screen->availableGeometry();
        pos.setX(qBound(pos.x(), ag.left(), ag.right() - m_menu->width()));
        pos.setY(qBound(pos.y(), ag.top(), ag.bottom() - m_menu->height()));
    }

    m_menu->move(pos);
    m_menu->show();
    m_menu->raise();
}
