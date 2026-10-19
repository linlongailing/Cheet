#pragma once

#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/** 浏览器：读地址栏、按 URL 切标签；没有目标站则新开标签加载。 */
namespace BrowserNav {

#ifdef Q_OS_WIN
bool isTargetUrl(const QString &url, const QString &site);
QString readAddressBarUrl(HWND hwnd);
bool selectTargetTabByUrl(HWND hwnd, const QString &browser, const QString &site);
bool openUrlInNewTab(HWND hwnd, const QString &url);
/** 当前标签跳转（不新开），用于离开误进的技能页等 */
bool navigateCurrentTab(HWND hwnd, const QString &url);
bool waitAddressBarTarget(HWND hwnd, const QString &site, int timeoutMs = 15000);

/** 聚焦已有浏览器并切到目标站标签；没有则启动/新开标签。返回窗口句柄，失败为 0。 */
HWND focusOrOpenTarget(const QString &browser, const QString &site, const QString &url,
                       bool *launched = nullptr);
#endif

} // namespace BrowserNav
