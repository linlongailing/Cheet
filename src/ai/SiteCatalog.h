#pragma once

#include <QString>
#include <QStringList>

/** 站点 / 浏览器常量与提示词表（供 WinInput、BrowserNav、UiaComposer 共用）。 */
namespace SiteCatalog {

inline QString normalizeSite(const QString &site)
{
    const QString s = site.trimmed().toLower();
    if (s == QLatin1String("gemini") || s == QLatin1String("claude")
        || s == QLatin1String("doubao") || s == QLatin1String("deepseek"))
        return s;
    // 千问 / 通义
    if (s == QLatin1String("qwen") || s == QLatin1String("qianwen")
        || s == QLatin1String("tongyi"))
        return QStringLiteral("qwen");
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
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("doubao.com"), QStringLiteral("www.doubao.com")};
    if (s == QLatin1String("qwen"))
        return {QStringLiteral("qianwen.com"), QStringLiteral("tongyi.aliyun.com"),
                QStringLiteral("tongyi.com"), QStringLiteral("chat.qwen.ai")};
    if (s == QLatin1String("deepseek"))
        return {QStringLiteral("chat.deepseek.com"), QStringLiteral("deepseek.com")};
    return {QStringLiteral("chatgpt.com"), QStringLiteral("chat.openai.com")};
}

/** 标签搜索只用第一个词（域名优先，一次命中即可）。 */
inline QStringList searchKeywords(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini.google.com")};
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude.ai")};
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("doubao.com")};
    if (s == QLatin1String("qwen"))
        return {QStringLiteral("qianwen.com")};
    if (s == QLatin1String("deepseek"))
        return {QStringLiteral("chat.deepseek.com")};
    return {QStringLiteral("chatgpt.com")};
}

inline QStringList tabTitleHints(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini"), QStringLiteral("双子座")};
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude")};
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("豆包"), QStringLiteral("doubao")};
    if (s == QLatin1String("qwen"))
        return {QStringLiteral("千问"), QStringLiteral("通义"), QStringLiteral("qwen"),
                QStringLiteral("qianwen")};
    if (s == QLatin1String("deepseek"))
        return {QStringLiteral("deepseek")};
    return {QStringLiteral("chatgpt"), QStringLiteral("chat gpt")};
}

inline QStringList desktopProcessNames(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude.exe"), QStringLiteral("claude-desktop.exe")};
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini.exe")};
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("Doubao.exe"), QStringLiteral("doubao.exe")};
    if (s == QLatin1String("qwen"))
        return {QStringLiteral("Qianwen.exe"), QStringLiteral("qianwen.exe"),
                QStringLiteral("QwenWorkCN.exe"), QStringLiteral("qwenwork.exe"),
                QStringLiteral("tongyi.exe")};
    if (s == QLatin1String("deepseek"))
        return {QStringLiteral("DeepSeek.exe"), QStringLiteral("deepseek.exe"),
                QStringLiteral("DeepSeek Chat.exe"), QStringLiteral("deepseek-desktop.exe"),
                QStringLiteral("dsh.exe")};
    return {QStringLiteral("chatgpt.exe")};
}

inline QStringList desktopTitleHints(const QString &site)
{
    return tabTitleHints(site);
}

/** 开始菜单 .lnk 名称匹配（各站统一走快捷方式解析）。 */
inline QStringList desktopShortcutHints(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("gemini"), QStringLiteral("双子座")};
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude")};
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("豆包"), QStringLiteral("doubao")};
    if (s == QLatin1String("qwen"))
        return {QStringLiteral("千问"), QStringLiteral("通义"), QStringLiteral("qianwen"),
                QStringLiteral("qwen")};
    if (s == QLatin1String("deepseek"))
        return {QStringLiteral("deepseek")};
    return {QStringLiteral("chatgpt"), QStringLiteral("chat gpt")};
}

/** 自定义协议（无 exe 路径时仍可唤起客户端）。 */
inline QStringList desktopProtocols(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("doubao"), QStringLiteral("doubaoapp")};
    if (s == QLatin1String("chatgpt"))
        return {QStringLiteral("chatgpt")}; // 若未注册则检测为空
    if (s == QLatin1String("claude"))
        return {QStringLiteral("claude")};
    return {};
}

/** 网页误入技能/生图页时需离开的 URL 片段。 */
inline QStringList avoidUrlHints(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("create-image"), QStringLiteral("image-generation")};
    return {};
}

inline QStringList desktopPackagePrefixes(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("claude"))
        return {QStringLiteral("Claude_"), QStringLiteral("AnthropicClaude")};
    if (s == QLatin1String("gemini"))
        return {QStringLiteral("Google.Gemini"), QStringLiteral("Gemini_")};
    if (s == QLatin1String("doubao"))
        return {QStringLiteral("ByteDance.Doubao"), QStringLiteral("Bytedance.Doubao"),
                QStringLiteral("Doubao.")};
    if (s == QLatin1String("qwen"))
        return {QStringLiteral("Qianwen"), QStringLiteral("Tongyi"), QStringLiteral("Alibaba.Qwen"),
                QStringLiteral("QwenWork")};
    if (s == QLatin1String("deepseek"))
        return {QStringLiteral("DeepSeek"), QStringLiteral("com.deepseek"),
                QStringLiteral("deepseek")};
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
    if (s == QLatin1String("doubao")) {
        return {QStringLiteral("prosemirror"), QStringLiteral("contenteditable"), QStringLiteral("输入"),
                QStringLiteral("发消息"), QStringLiteral("问问豆包"), QStringLiteral("有问题"),
                QStringLiteral("豆包"), QStringLiteral("doubao"), QStringLiteral("message"),
                QStringLiteral("随便问"), QStringLiteral("说点什么")};
    }
    if (s == QLatin1String("qwen")) {
        return {QStringLiteral("prosemirror"), QStringLiteral("contenteditable"), QStringLiteral("输入"),
                QStringLiteral("有问题"), QStringLiteral("问问"), QStringLiteral("千问"),
                QStringLiteral("通义"), QStringLiteral("qwen"), QStringLiteral("message")};
    }
    if (s == QLatin1String("deepseek")) {
        return {QStringLiteral("prosemirror"), QStringLiteral("contenteditable"), QStringLiteral("输入"),
                QStringLiteral("ask"), QStringLiteral("deepseek"), QStringLiteral("给 deepseek"),
                QStringLiteral("message"), QStringLiteral("chat")};
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
    if (s == QLatin1String("doubao"))
        return QStringLiteral("https://www.doubao.com/chat/");
    if (s == QLatin1String("qwen"))
        return QStringLiteral("https://www.qianwen.com/");
    if (s == QLatin1String("deepseek"))
        return QStringLiteral("https://chat.deepseek.com/");
    return QStringLiteral("https://chatgpt.com");
}

inline QString siteLabel(const QString &site)
{
    const QString s = normalizeSite(site);
    if (s == QLatin1String("gemini"))
        return QStringLiteral("Gemini");
    if (s == QLatin1String("claude"))
        return QStringLiteral("Claude");
    if (s == QLatin1String("doubao"))
        return QStringLiteral("豆包");
    if (s == QLatin1String("qwen"))
        return QStringLiteral("千问");
    if (s == QLatin1String("deepseek"))
        return QStringLiteral("DeepSeek");
    return QStringLiteral("ChatGPT");
}

/** 设置页下拉用：qrc 图标路径（相对 CheetahClip 资源）。 */
inline QString siteIconPath(const QString &site)
{
    const QString s = normalizeSite(site);
    return QStringLiteral("qrc:/qt/qml/CheetahClip/resources/icons/sites/%1.png").arg(s);
}

/** 豆包/千问/DeepSeek：桌面端与网页端 UIA 都不可靠，忌乱点按钮，走简单粘贴。 */
inline bool needsSimplePaste(const QString &site)
{
    const QString s = normalizeSite(site);
    return s == QLatin1String("doubao") || s == QLatin1String("qwen")
        || s == QLatin1String("deepseek");
}

/** @deprecated 使用 needsSimplePaste */
inline bool isChromiumDesktopShell(const QString &site)
{
    return needsSimplePaste(site);
}

} // namespace SiteCatalog
