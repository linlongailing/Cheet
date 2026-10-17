#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    bool registerHotkey(const QString &hotkey);
    void unregisterHotkey();
    QString currentHotkey() const { return m_hotkey; }

signals:
    void activated();

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    bool parseHotkey(const QString &hotkey, unsigned int &modifiers, unsigned int &vk) const;

    QString m_hotkey;
    int m_id = 1;
    bool m_registered = false;
};
