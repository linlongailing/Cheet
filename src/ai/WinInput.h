#pragma once

#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/** Win32 前台、按键与浏览器/桌面端窗口定位。 */
namespace WinInput {

#ifdef Q_OS_WIN
bool setForeground(HWND hwnd);
void sendCtrlKey(WORD vk);          // Ctrl+V / Ctrl+A / Ctrl+End 等，vk 为主键
void sendCtrlShiftKey(WORD vk);     // Ctrl+Shift+A 等
void sendKey(WORD vk);              // 单键按下抬起（Escape / Down / PageDown…）
void sendEnter();
void sendText(const QString &text); // Unicode 逐字输入（慢），优先用剪贴板
/**
 * 屏幕坐标单击。
 * moveCursor=false（默认）：发窗口消息点击，指针完全不动（无闪烁）。
 * moveCursor=true：真实移动指针点击（仅兜底，会闪一下）。
 */
void clickScreen(int x, int y, bool moveCursor = false);
void sleepMs(int ms);
HWND findBrowserWindow(const QString &browser); // edge/chrome/firefox
HWND findDesktopWindow(const QString &site);    // chatgpt/gemini/claude/doubao/qwen/deepseek
bool launchBrowser(const QString &browser, const QString &url);
bool launchDesktop(const QString &site);
QString findBrowserExe(const QString &browser);
QString findDesktopExe(const QString &site);
QString findDesktopAumid(const QString &site); // Store 包 AUMID，如 OpenAI.Codex_xxx!App
bool isDesktopInstalled(const QString &site);  // exe / AUMID / 协议 / 已运行窗口
void invalidateDesktopCache();                 // 设置变更后清缓存
#endif

} // namespace WinInput
