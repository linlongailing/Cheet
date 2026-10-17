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
};

#ifdef Q_OS_WIN
void enableWebAccessibility(HWND hwnd);
bool focusComposer(HWND hwnd, const QString &site);
State scan(HWND hwnd, const QString &site);
bool waitAttachmentAdded(HWND hwnd, const QString &site, int before, int timeoutMs = 4000);
bool waitUntilUploaded(HWND hwnd, const QString &site, int minWaitMs, int maxWaitMs = 120000);
bool messageSent(HWND hwnd, const QString &site, const QString &prompt);
void clearComposer(HWND hwnd, const QString &site);
#endif

} // namespace UiaComposer
