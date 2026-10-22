#pragma once

#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/** Composer 输入区扫描与图片上传完成监测。 */
namespace UiaComposer {

struct State {
    bool composerFound = false;
    int attachments = 0;
    bool uploading = false;
    bool sendFound = false;
    bool sendEnabled = false;
    bool imageBlocked = false; // 免费版限制 / 不支持图片等
};

/** 粘贴图片后的探测结果：失败或受限时不得继续贴文/发送。 */
enum class ImagePasteResult {
    Ok,
    Blocked, // 免费版/套餐限制或不支持图片
    Failed,  // 未能确认图片已附上
    WindowGone,
};

#ifdef Q_OS_WIN
void enableWebAccessibility(HWND hwnd);
bool focusComposer(HWND hwnd, const QString& site);
/** 是否真的找到了输入框控件（区别于坐标盲点：页面没渲染完时返回 false）。 */
bool composerPresent(HWND hwnd, const QString& site);
State scan(HWND hwnd, const QString& site);
/** 全窗探测附件数与限制文案（含 toast/对话框）。 */
State probeWindow(HWND hwnd);
bool waitAttachmentAdded(HWND hwnd, const QString& site, int before, int timeoutMs = 4000);
/** 粘贴图片后等待：确认附上 / 发现限制 / 超时失败。
 *  「已贴上」只认输入区（scan）附件数超过 beforeAttachments；全窗计数含
 *  弹窗/历史图片，绝不作为贴上依据，仅借它探测限制文案。
 *  beforeWindowAttachments 为历史参数，已不参与判定。 */
ImagePasteResult waitImagePasteResult(HWND hwnd,
                                      const QString& site,
                                      int beforeAttachments,
                                      int timeoutMs = 8000,
                                      int beforeWindowAttachments = -1);
/** 等图片上传完成。expectedAttachments>=0 时监测附件是否被用户删除：
 *  连续两轮扫描附件数低于期望值则置 *attachmentRemoved 并立即返回 false。 */
bool waitUntilUploaded(HWND hwnd,
                       const QString& site,
                       int minWaitMs,
                       int maxWaitMs = 120000,
                       int expectedAttachments = -1,
                       bool* attachmentRemoved = nullptr);
bool messageSent(HWND hwnd, const QString& site, const QString& prompt);
void clearComposer(HWND hwnd, const QString& site);
#endif

} // namespace UiaComposer
