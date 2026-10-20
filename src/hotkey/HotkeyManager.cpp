#include "hotkey/HotkeyManager.h"

#include <QAbstractEventDispatcher>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
    if (auto* dispatcher = QAbstractEventDispatcher::instance())
        dispatcher->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager() {
    unregisterHotkey();
    if (auto* dispatcher = QAbstractEventDispatcher::instance())
        dispatcher->removeNativeEventFilter(this);
}

bool HotkeyManager::parseHotkey(const QString& hotkey,
                                unsigned int& modifiers,
                                unsigned int& vk) const {
#ifdef Q_OS_WIN
    modifiers = 0;
    vk = 0;
    const auto parts = hotkey.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QString key;
    for (QString p : parts) {
        p = p.trimmed();
        if (p == QLatin1String("ctrl") || p == QLatin1String("control"))
            modifiers |= MOD_CONTROL;
        else if (p == QLatin1String("alt"))
            modifiers |= MOD_ALT;
        else if (p == QLatin1String("shift"))
            modifiers |= MOD_SHIFT;
        else if (p == QLatin1String("win") || p == QLatin1String("windows"))
            modifiers |= MOD_WIN;
        else
            key = p;
    }
    if (modifiers == 0 || key.isEmpty())
        return false;
    if (key.size() == 1) {
        const QChar c = key.at(0).toUpper();
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
            (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            vk = static_cast<UINT>(c.unicode());
    } else if (key.startsWith(QLatin1String("f")) && key.size() <= 3) {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            vk = VK_F1 + static_cast<UINT>(n - 1);
    }
    return vk != 0;
#else
    Q_UNUSED(hotkey);
    Q_UNUSED(modifiers);
    Q_UNUSED(vk);
    return false;
#endif
}

bool HotkeyManager::registerHotkey(const QString& hotkey) {
    unregisterHotkey();
#ifdef Q_OS_WIN
    unsigned int modifiers = 0;
    unsigned int vk = 0;
    if (!parseHotkey(hotkey, modifiers, vk))
        return false;
    if (!RegisterHotKey(nullptr, m_id, modifiers | MOD_NOREPEAT, vk))
        return false;
    m_hotkey = hotkey;
    m_registered = true;
    return true;
#else
    Q_UNUSED(hotkey);
    return false;
#endif
}

void HotkeyManager::unregisterHotkey() {
#ifdef Q_OS_WIN
    if (m_registered) {
        UnregisterHotKey(nullptr, m_id);
        m_registered = false;
    }
#endif
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const MSG* msg = static_cast<MSG*>(message);
        if (msg && msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == m_id) {
            emit activated();
            return false;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}
