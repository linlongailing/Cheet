#pragma once

#include <QString>
#include <QStringList>

/** 站点 / 浏览器常量与提示词表（供 WinInput、BrowserNav、UiaComposer 共用）。 */
namespace SiteCatalog {

inline QString normalizeSite(const QString &site)
{
    const QString s = site.trimmed().toLower();
    if (s == QLatin1String("gemini") || s == QLatin1String("claude"))
        return s;
    return QStringLiteral("chatgpt");
}

inline QString normalizeBrowser(const QString &browser)
{
    const QString b = browser.trimmed().toLower();
    if (b == QLatin1String("chrome") || b == QLatin1String("firefox"))
        return b;
    return QStringLiteral("edge");
}

inline QStringList urlHints(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini.google.com"), QStringLiteral("bard.google.com")};
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude.ai")};
    return {QStringLiteral("chatgpt.com"), QStringLiteral("chat.openai.com")};
}

inline QStringList searchKeywords(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("Gemini"), QStringLiteral("gemini.google.com"), QStringLiteral("gemini")};
    if (s == QLatin1String("claude"))
        return {QStringLiteral("Claude"), QStringLiteral("claude.ai")};
    return {QStringLiteral("chatgpt.com"), QStringLiteral("ChatGPT"), QStringLiteral("chatgpt")};
}

inline QStringList tabTitleHints(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini"), QStringLiteral("双子座")};
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude")};
    return {QStringLiteral("chatgpt"), QStringLiteral("chat gpt")};
}

inline QStringList desktopProcessNames(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude.exe"), QStringLiteral("claude-desktop.exe")};
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini.exe")};
    return {QStringLiteral("chatgpt.exe")};
}

inline QStringList desktopTitleHints(const QString &site)
{
    return tabTitleHints(site);
}

inline QStringList desktopPackagePrefixes(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("claude"))
        return {QStringLiteral("Claude_"), QStringLiteral("AnthropicClaude")};
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("Google.Gemini"), QStringLiteral("Gemini_")};
    return {QStringLiteral("OpenAI.ChatGPT"), QStringLiteral("OpenAI.Codex")};
}

inline QStringList composerHints(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini")) {
        return {QStringLiteral("ql-editor"), QStringLiteral("输入提示"), QStringLiteral("在此输入"),
                QStringLiteral("enter a prompt"), QStringLiteral("ask gemini"), QStringLiteral("问问 gemini")};
    }
    if (s == QLatin1String("claude")) {
        return {QStringLiteral("prosemirror"), QStringLiteral("claude"), QStringLiteral("how can i help"),
                QStringLiteral("reply"), QStringLiteral("回复"), QStringLiteral("向 claude")};
    }
    return {QStringLiteral("prosemirror"), QStringLiteral("询问"), QStringLiteral("有问题"),
            QStringLiteral("随便问"), QStringLiteral("ask anything"), QStringLiteral("ask chatgpt"),
            QStringLiteral("message")};
}

inline QString targetUrl(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return QStringLiteral("https://gemini.google.com/app");
    if (s == QLatin1String("claude"))
        return QStringLiteral("https://claude.ai/new");
    return QStringLiteral("https://chatgpt.com");
}

inline QString siteLabel(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return QStringLiteral("Gemini");
    if (s == QLatin1String("claude"))
        return QStringLiteral("Claude");
    return QStringLiteral("ChatGPT");
}

} // namespace SiteCatalog
