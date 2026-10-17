#pragma once

#if defined(_WIN32) || defined(Q_OS_WIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>

#include "platform/UiaCore.h"

namespace platform {

/** 当前线程的 COM 公寓初始化（RAII）。可嵌套；仅最外层负责 Uninitialize。 */
class ComApartment
{
public:
    explicit ComApartment(DWORD coInit = COINIT_APARTMENTTHREADED)
    {
        const HRESULT hr = CoInitializeEx(nullptr, coInit);
        m_shouldUninit = (hr == S_OK || hr == S_FALSE);
        // RPC_E_CHANGED_MODE：已有公寓但模式不同，不能当成功使用 UIA
        m_ok = (hr == S_OK || hr == S_FALSE);
    }

    ~ComApartment()
    {
        if (m_shouldUninit) {
            // 必须先释放 thread_local UIA，再 CoUninitialize
            UiaCore::instance().shutdown();
            CoUninitialize();
        }
    }

    ComApartment(const ComApartment &) = delete;
    ComApartment &operator=(const ComApartment &) = delete;

    bool ok() const { return m_ok; }

private:
    bool m_shouldUninit = false;
    bool m_ok = false;
};

} // namespace platform

#endif // _WIN32
