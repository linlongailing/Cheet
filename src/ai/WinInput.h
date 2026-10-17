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
void sleepMs(int ms);
HWND findBrowserWindow(const QString &browser); // edge/chrome/firefox
HWND findDesktopWindow(const QString &site);    // chatgpt/gemini/claude
bool launchBrowser(const QString &browser, const QString &url);
bool launchDesktop(const QString &site);
QString findBrowserExe(const QString &browser);
QString findDesktopExe(const QString &site);
QString findDesktopAumid(const QString &site); // Store 包 AUMID，如 OpenAI.Codex_xxx!App
bool isDesktopInstalled(const QString &site);  // exe / AUMID / 已运行窗口
#endif

} // namespace WinInput
