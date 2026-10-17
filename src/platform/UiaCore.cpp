#include "platform/UiaCore.h"

#if defined(_WIN32)

namespace platform {

UiaCore &UiaCore::instance()
{
    // 每线程一份：与 STA 公寓及工作线程 COM 初始化匹配
    thread_local UiaCore core;
    return core;
}

UiaCore::UiaCore()
{
    ensureCreated();
}

void UiaCore::ensureCreated()
{
    if (m_uia)
        return;
    IUIAutomation *raw = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_IUIAutomation, reinterpret_cast<void **>(&raw));
    if (SUCCEEDED(hr) && raw)
        m_uia.attach(raw);
}

void UiaCore::shutdown()
{
    m_walker.reset();
    m_uia.reset();
}

IUIAutomation *UiaCore::automation()
{
    ensureCreated();
    return m_uia.get();
}

bool UiaCore::ready()
{
    ensureCreated();
    return static_cast<bool>(m_uia);
}

IUIAutomationTreeWalker *UiaCore::controlWalker()
{
    ensureCreated();
    if (!m_uia)
        return nullptr;
    if (!m_walker) {
        IUIAutomationTreeWalker *w = nullptr;
        if (SUCCEEDED(m_uia->get_ControlViewWalker(&w)) && w)
            m_walker.attach(w);
    }
    return m_walker.get();
}

ComPtr<IUIAutomationElement> UiaCore::elementFromHwnd(HWND hwnd)
{
    ensureCreated();
    ComPtr<IUIAutomationElement> el;
    if (!m_uia || !windowAlive(hwnd))
        return el;
    IUIAutomationElement *raw = nullptr;
    if (SUCCEEDED(m_uia->ElementFromHandle(hwnd, &raw)) && raw)
        el.attach(raw);
    return el;
}

} // namespace platform

#endif // _WIN32
