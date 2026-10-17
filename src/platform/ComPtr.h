#pragma once

#if defined(_WIN32) || defined(Q_OS_WIN)

#include <utility>

namespace platform {

/** 轻量 COM 智能指针（AddRef/Release，移动语义）。 */
template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    explicit ComPtr(T *p) noexcept
        : m_ptr(p)
    {
    }

    ComPtr(const ComPtr &other) noexcept
        : m_ptr(other.m_ptr)
    {
        if (m_ptr)
            m_ptr->AddRef();
    }

    ComPtr(ComPtr &&other) noexcept
        : m_ptr(other.m_ptr)
    {
        other.m_ptr = nullptr;
    }

    ~ComPtr() { reset(); }

    ComPtr &operator=(const ComPtr &other) noexcept
    {
        if (this != &other)
            reset(other.m_ptr);
        return *this;
    }

    ComPtr &operator=(ComPtr &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    ComPtr &operator=(T *p) noexcept
    {
        reset(p);
        return *this;
    }

    void reset(T *p = nullptr) noexcept
    {
        if (p)
            p->AddRef();
        if (m_ptr)
            m_ptr->Release();
        m_ptr = p;
    }

    /** 接管已 AddRef 的指针（不再额外 AddRef）。 */
    void attach(T *p) noexcept
    {
        if (m_ptr)
            m_ptr->Release();
        m_ptr = p;
    }

    T *detach() noexcept
    {
        T *p = m_ptr;
        m_ptr = nullptr;
        return p;
    }

    T *get() const noexcept { return m_ptr; }
    T **put() noexcept
    {
        reset();
        return &m_ptr;
    }

    T *operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

private:
    T *m_ptr = nullptr;
};

} // namespace platform

#endif // _WIN32
