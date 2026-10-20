#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariantMap>

class I18n : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
public:
    static I18n& instance();
    static QString defaultPrompt(const QString& lang);

    QString language() const;
    void setLanguage(const QString& lang);

    Q_INVOKABLE QString tr(const QString& key, const QVariantMap& args = {}) const;

signals:
    void languageChanged();

private:
    explicit I18n(QObject* parent = nullptr);
    // m_language 会被 AI 工作线程（tr/language）与 GUI 线程（setLanguage）并发访问，须加锁；
    // m_strings 仅构造时填充、之后只读，天然线程安全
    mutable QMutex m_languageMutex;
    QString m_language = QStringLiteral("zh");
    QHash<QString, QHash<QString, QString>> m_strings;
};
