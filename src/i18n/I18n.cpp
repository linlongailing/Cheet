#include "i18n/I18n.h"

I18n &I18n::instance()
{
    static I18n inst;
    return inst;
}

QString I18n::defaultPrompt(const QString &lang)
{
    return lang == QLatin1String("en") ? QStringLiteral("Explain this image in detail")
                                       : QStringLiteral("详解图片内容");
}

I18n::I18n(QObject *parent)
    : QObject(parent)
{
    auto &zh = m_strings[QStringLiteral("zh")];
    zh[QStringLiteral("settings_title")] = QStringLiteral("Cheet 设置");
    zh[QStringLiteral("settings_heading")] = QStringLiteral("设置");
    zh[QStringLiteral("settings_subtitle")] = QStringLiteral("管理截图方式、AI 问询与应用偏好。");
    zh[QStringLiteral("settings_local")] = QStringLiteral("本地配置");
    zh[QStringLiteral("section_capture")] = QStringLiteral("01  截图与快捷键");
    zh[QStringLiteral("section_capture_desc")] = QStringLiteral("设定截图入口与选区尺寸");
    zh[QStringLiteral("section_ai")] = QStringLiteral("02  AI 问询");
    zh[QStringLiteral("section_ai_desc")] = QStringLiteral("选择模型与问询方式");
    zh[QStringLiteral("section_general")] = QStringLiteral("03  常规");
    zh[QStringLiteral("language")] = QStringLiteral("界面语言");
    zh[QStringLiteral("hotkey")] = QStringLiteral("截图快捷键");
    zh[QStringLiteral("reset_default")] = QStringLiteral("恢复默认");
    zh[QStringLiteral("hotkey_hint")] = QStringLiteral("点击输入框，按下组合键即可设置");
    zh[QStringLiteral("hotkey_press")] = QStringLiteral("请按下新的组合键…");
    zh[QStringLiteral("hotkey_need_modifier")] = QStringLiteral("请使用组合键（如 Alt+Q），不能只用单个字母");
    zh[QStringLiteral("hotkey_recorded")] = QStringLiteral("已记录，点「保存」生效");
    zh[QStringLiteral("hotkey_reset")] = QStringLiteral("已恢复默认，点「保存」生效");
    zh[QStringLiteral("select_mode")] = QStringLiteral("截图框选模式");
    zh[QStringLiteral("mode_free")] = QStringLiteral("自由拖拽框选");
    zh[QStringLiteral("mode_fixed")] = QStringLiteral("固定宽高框选");
    zh[QStringLiteral("fixed_width")] = QStringLiteral("固定宽度 (px)");
    zh[QStringLiteral("fixed_height")] = QStringLiteral("固定高度 (px)");
    zh[QStringLiteral("browser")] = QStringLiteral("默认浏览器");
    zh[QStringLiteral("not_detected")] = QStringLiteral("{label}（未检测到）");
    zh[QStringLiteral("site")] = QStringLiteral("模型");
    zh[QStringLiteral("use_desktop")] = QStringLiteral("优先使用桌面客户端问询");
    zh[QStringLiteral("desktop_hint_off")] = QStringLiteral("未开启时始终使用浏览器；截图页点「AI问询」才会问询");
    zh[QStringLiteral("desktop_hint_found")] = QStringLiteral("已检测到 {site} 桌面客户端，问询时会优先打开它");
    zh[QStringLiteral("desktop_hint_missing")] = QStringLiteral("未检测到 {site} 桌面客户端，开启后将无法问询（不会改用浏览器）");
    zh[QStringLiteral("prompt")] = QStringLiteral("AI 提示词");
    zh[QStringLiteral("autostart")] = QStringLiteral("开机启动");
    zh[QStringLiteral("save")] = QStringLiteral("保存");
    zh[QStringLiteral("cancel")] = QStringLiteral("取消");
    zh[QStringLiteral("tray_capture")] = QStringLiteral("立即截图 ({hotkey})");
    zh[QStringLiteral("tray_settings")] = QStringLiteral("设置");
    zh[QStringLiteral("tray_quit")] = QStringLiteral("退出");
    zh[QStringLiteral("notify_copied")] = QStringLiteral("截图已复制到剪贴板");
    zh[QStringLiteral("notify_opening")] = QStringLiteral("正在打开 {label}…");
    zh[QStringLiteral("notify_browser_missing")] = QStringLiteral("未检测到 {browser}，请安装或更换浏览器。截图已在剪贴板中。");
    zh[QStringLiteral("notify_open_failed")] = QStringLiteral("未能打开 {label}，截图已在剪贴板中。");
    zh[QStringLiteral("notify_sent")] = QStringLiteral("已发送到 {site}");
    zh[QStringLiteral("notify_stopped")] = QStringLiteral("{browser} 已关闭或发送中断，截图仍在剪贴板中。");
    zh[QStringLiteral("notify_desktop_stopped")] =
        QStringLiteral("{site} 桌面客户端已关闭，已停止自动发送，截图仍在剪贴板中。");
    zh[QStringLiteral("notify_desktop_missing")] =
        QStringLiteral("未检测到 {site} 桌面客户端，已停止自动发送，截图仍在剪贴板中。");
    zh[QStringLiteral("notify_desktop_fallback")] = QStringLiteral("未找到 {site} 桌面客户端，改用浏览器…");
    zh[QStringLiteral("notify_failed")] = QStringLiteral("截图发送失败：{err}");
    zh[QStringLiteral("notify_sending")] = QStringLiteral("正在粘贴图片并发送…");
    zh[QStringLiteral("notify_image_blocked")] =
        QStringLiteral("{site} 无法接收图片（未登录/套餐限制或不支持），已取消贴字与发送。截图仍在剪贴板中。");
    zh[QStringLiteral("notify_image_failed")] =
        QStringLiteral("{site} 未能附上图片（可能未登录），已取消贴字与发送。截图仍在剪贴板中。");
    zh[QStringLiteral("capture_tip")] =
        QStringLiteral("拖拽框选；用工具栏标注，点「AI问询」发送，或 ✓ 仅复制");
    zh[QStringLiteral("tool_rect")] = QStringLiteral("矩形");
    zh[QStringLiteral("tool_rect_desc")] = QStringLiteral("拖拽绘制矩形框，用于圈出区域");
    zh[QStringLiteral("tool_ellipse")] = QStringLiteral("椭圆");
    zh[QStringLiteral("tool_ellipse_desc")] = QStringLiteral("拖拽绘制椭圆/圆形，用于圈出区域");
    zh[QStringLiteral("tool_arrow")] = QStringLiteral("箭头");
    zh[QStringLiteral("tool_arrow_desc")] = QStringLiteral("拖拽绘制箭头，用于指示位置");
    zh[QStringLiteral("tool_pen")] = QStringLiteral("画笔");
    zh[QStringLiteral("tool_pen_desc")] = QStringLiteral("自由绘制线条，用于手写标注");
    zh[QStringLiteral("tool_text")] = QStringLiteral("文字");
    zh[QStringLiteral("tool_text_desc")] = QStringLiteral("点击选区后输入文字说明");
    zh[QStringLiteral("tool_undo")] = QStringLiteral("撤销");
    zh[QStringLiteral("tool_undo_desc")] = QStringLiteral("撤销上一步标注操作");
    zh[QStringLiteral("tool_cancel")] = QStringLiteral("取消");
    zh[QStringLiteral("tool_cancel_desc")] = QStringLiteral("放弃本次截图");
    zh[QStringLiteral("tool_ai")] = QStringLiteral("AI 问询");
    zh[QStringLiteral("tool_ai_desc")] = QStringLiteral("确认截图并按设置发送到桌面客户端或浏览器");
    zh[QStringLiteral("tool_ok")] = QStringLiteral("完成");
    zh[QStringLiteral("tool_ok_desc")] = QStringLiteral("确认截图，只复制到剪贴板，不自动发送");

    auto &en = m_strings[QStringLiteral("en")];
    en[QStringLiteral("settings_title")] = QStringLiteral("Cheet Settings");
    en[QStringLiteral("settings_heading")] = QStringLiteral("Settings");
    en[QStringLiteral("settings_subtitle")] = QStringLiteral("Manage capture, AI inquire, and app preferences.");
    en[QStringLiteral("settings_local")] = QStringLiteral("Local config");
    en[QStringLiteral("section_capture")] = QStringLiteral("01  Capture & hotkey");
    en[QStringLiteral("section_capture_desc")] = QStringLiteral("Hotkey and selection size");
    en[QStringLiteral("section_ai")] = QStringLiteral("02  AI inquire");
    en[QStringLiteral("section_ai_desc")] = QStringLiteral("Choose model and inquire method");
    en[QStringLiteral("section_general")] = QStringLiteral("03  General");
    en[QStringLiteral("language")] = QStringLiteral("Language");
    en[QStringLiteral("hotkey")] = QStringLiteral("Capture hotkey");
    en[QStringLiteral("reset_default")] = QStringLiteral("Reset");
    en[QStringLiteral("hotkey_hint")] = QStringLiteral("Click the box and press a key combination");
    en[QStringLiteral("hotkey_press")] = QStringLiteral("Press a new combination…");
    en[QStringLiteral("hotkey_need_modifier")] = QStringLiteral("Use a combination (e.g. Alt+Q), not a single letter");
    en[QStringLiteral("hotkey_recorded")] = QStringLiteral("Recorded — click Save to apply");
    en[QStringLiteral("hotkey_reset")] = QStringLiteral("Reset — click Save to apply");
    en[QStringLiteral("select_mode")] = QStringLiteral("Selection mode");
    en[QStringLiteral("mode_free")] = QStringLiteral("Free drag selection");
    en[QStringLiteral("mode_fixed")] = QStringLiteral("Fixed size selection");
    en[QStringLiteral("fixed_width")] = QStringLiteral("Fixed width (px)");
    en[QStringLiteral("fixed_height")] = QStringLiteral("Fixed height (px)");
    en[QStringLiteral("browser")] = QStringLiteral("Default browser");
    en[QStringLiteral("not_detected")] = QStringLiteral("{label} (not detected)");
    en[QStringLiteral("site")] = QStringLiteral("Model");
    en[QStringLiteral("use_desktop")] = QStringLiteral("Prefer the desktop app for inquire");
    en[QStringLiteral("desktop_hint_off")] = QStringLiteral("When off, always use the browser; inquire only via AI Inquire");
    en[QStringLiteral("desktop_hint_found")] = QStringLiteral("{site} desktop app detected — will open it first");
    en[QStringLiteral("desktop_hint_missing")] =
        QStringLiteral("{site} desktop app not found — inquire will not fall back to the browser");
    en[QStringLiteral("prompt")] = QStringLiteral("AI prompt");
    en[QStringLiteral("autostart")] = QStringLiteral("Start with Windows");
    en[QStringLiteral("save")] = QStringLiteral("Save");
    en[QStringLiteral("cancel")] = QStringLiteral("Cancel");
    en[QStringLiteral("tray_capture")] = QStringLiteral("Capture now ({hotkey})");
    en[QStringLiteral("tray_settings")] = QStringLiteral("Settings");
    en[QStringLiteral("tray_quit")] = QStringLiteral("Quit");
    en[QStringLiteral("notify_copied")] = QStringLiteral("Screenshot copied to clipboard");
    en[QStringLiteral("notify_opening")] = QStringLiteral("Opening {label}…");
    en[QStringLiteral("notify_browser_missing")] = QStringLiteral("{browser} not found. Screenshot is on the clipboard.");
    en[QStringLiteral("notify_open_failed")] = QStringLiteral("Could not open {label}. Screenshot is on the clipboard.");
    en[QStringLiteral("notify_sent")] = QStringLiteral("Sent to {site}");
    en[QStringLiteral("notify_stopped")] = QStringLiteral("{browser} closed or send interrupted. Screenshot is on the clipboard.");
    en[QStringLiteral("notify_desktop_stopped")] =
        QStringLiteral("The {site} desktop app was closed, so auto-send stopped. The screenshot is still on the clipboard.");
    en[QStringLiteral("notify_desktop_missing")] =
        QStringLiteral("{site} desktop app not found; auto-send stopped. The screenshot is still on the clipboard.");
    en[QStringLiteral("notify_desktop_fallback")] = QStringLiteral("{site} desktop app not found — using browser…");
    en[QStringLiteral("notify_failed")] = QStringLiteral("Failed to send screenshot: {err}");
    en[QStringLiteral("notify_sending")] = QStringLiteral("Pasting image and sending…");
    en[QStringLiteral("notify_image_blocked")] =
        QStringLiteral("{site} cannot accept images (not signed in / plan limit). Text paste and send cancelled; screenshot is still on the clipboard.");
    en[QStringLiteral("notify_image_failed")] =
        QStringLiteral("Could not attach the image in {site} (maybe not signed in). Text paste and send cancelled; screenshot is still on the clipboard.");
    en[QStringLiteral("capture_tip")] =
        QStringLiteral("Drag to select; annotate with the toolbar, AI Inquire to send, or ✓ to copy only");
    en[QStringLiteral("tool_rect")] = QStringLiteral("Rectangle");
    en[QStringLiteral("tool_rect_desc")] = QStringLiteral("Drag to draw a rectangle around an area");
    en[QStringLiteral("tool_ellipse")] = QStringLiteral("Ellipse");
    en[QStringLiteral("tool_ellipse_desc")] = QStringLiteral("Drag to draw an ellipse or circle around an area");
    en[QStringLiteral("tool_arrow")] = QStringLiteral("Arrow");
    en[QStringLiteral("tool_arrow_desc")] = QStringLiteral("Drag to draw an arrow pointing at something");
    en[QStringLiteral("tool_pen")] = QStringLiteral("Pen");
    en[QStringLiteral("tool_pen_desc")] = QStringLiteral("Draw freehand lines");
    en[QStringLiteral("tool_text")] = QStringLiteral("Text");
    en[QStringLiteral("tool_text_desc")] = QStringLiteral("Click inside the selection to add a text note");
    en[QStringLiteral("tool_undo")] = QStringLiteral("Undo");
    en[QStringLiteral("tool_undo_desc")] = QStringLiteral("Undo the last annotation");
    en[QStringLiteral("tool_cancel")] = QStringLiteral("Cancel");
    en[QStringLiteral("tool_cancel_desc")] = QStringLiteral("Discard this screenshot");
    en[QStringLiteral("tool_ai")] = QStringLiteral("AI ask");
    en[QStringLiteral("tool_ai_desc")] = QStringLiteral("Confirm and send to the desktop app or browser per Settings");
    en[QStringLiteral("tool_ok")] = QStringLiteral("Done");
    en[QStringLiteral("tool_ok_desc")] = QStringLiteral("Confirm and copy to the clipboard only; do not send");
}

void I18n::setLanguage(const QString &lang)
{
    const QString n = (lang == QLatin1String("en")) ? QStringLiteral("en") : QStringLiteral("zh");
    if (m_language == n)
        return;
    m_language = n;
    emit languageChanged();
}

QString I18n::tr(const QString &key, const QVariantMap &args) const
{
    QString text = m_strings.value(m_language).value(key);
    if (text.isEmpty())
        text = m_strings.value(QStringLiteral("zh")).value(key, key);
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
        text.replace(QStringLiteral("{") + it.key() + QStringLiteral("}"), it.value().toString());
    return text;
}
