#pragma once

#if defined(_WIN32) || defined(Q_OS_WIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>

#include "platform/UiaCore.h"

namespace platform {

/** 当前线程的 COM 公寓初始化（RAII）。可嵌套：每次成功 CoInitializeEx 都配对
 *  CoUninitialize 以平衡 COM 引用计数；thread_local UIA 仅在最外层析构时释放，
 *  避免内层对象析构后外层仍在用 UIA 时拿到悬空指针。 */
class ComApartment {
public:
    explicit ComApartment(DWORD coInit = COINIT_APARTMENTTHREADED) {
        const HRESULT hr = CoInitializeEx(nullptr, coInit);
        m_shouldUninit = (hr == S_OK || hr == S_FALSE);
        // RPC_E_CHANGED_MODE：已有公寓但模式不同，不能当成功使用 UIA
        m_ok = (hr == S_OK || hr == S_FALSE);
        if (m_ok)
            ++depthRef();
    }

    ~ComApartment() {
        if (m_shouldUninit) {
            if (--depthRef() == 0)
                UiaCore::instance().shutdown();
            CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    bool ok() const { return m_ok; }

private:
    static int& depthRef() {
        static thread_local int depth = 0;
        return depth;
    }

    bool m_shouldUninit = false;
    bool m_ok = false;
};

} // namespace platform

#endif // _WIN32
