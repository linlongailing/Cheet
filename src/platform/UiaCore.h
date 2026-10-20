#pragma once

#if defined(_WIN32) || defined(Q_OS_WIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif

// UIAutomation 需要完整 COM 接口声明，不能在 WIN32_LEAN_AND_MEAN 下残缺包含
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif

// clang-format off
// 顺序不可由 SortIncludes 调整：UIAutomation.h 依赖 windows.h/objbase.h 已先行包含
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
// clang-format on

#include "platform/ComPtr.h"

namespace platform {

/** 进程内唯一的 UI Automation 入口；复用 ControlViewWalker。 */
class UiaCore {
public:
    static UiaCore& instance();

    /** 在 CoUninitialize 之前调用，释放本线程 UIA 对象，避免悬空 COM。 */
    void shutdown();

    IUIAutomation* automation();
    IUIAutomationTreeWalker* controlWalker();

    ComPtr<IUIAutomationElement> elementFromHwnd(HWND hwnd);

    bool ready();

private:
    UiaCore();
    ~UiaCore() = default;
    UiaCore(const UiaCore&) = delete;
    UiaCore& operator=(const UiaCore&) = delete;

    void ensureCreated();

    ComPtr<IUIAutomation> m_uia;
    ComPtr<IUIAutomationTreeWalker> m_walker;
};

inline bool windowAlive(HWND hwnd) {
    return hwnd != nullptr && IsWindow(hwnd) != FALSE;
}

} // namespace platform

#endif // _WIN32
