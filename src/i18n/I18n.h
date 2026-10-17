#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

class I18n : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
public:
    static I18n &instance();
    static QString defaultPrompt(const QString &lang);

    QString language() const { return m_language; }
    void setLanguage(const QString &lang);

    Q_INVOKABLE QString tr(const QString &key, const QVariantMap &args = {}) const;

signals:
    void languageChanged();

private:
    explicit I18n(QObject *parent = nullptr);
    QString m_language = QStringLiteral("zh");
    QHash<QString, QHash<QString, QString>> m_strings;
};
