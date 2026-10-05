#pragma once

// A value that can only be reached while holding its mutex.
//
//   Lockable<sol::state, std::recursive_mutex> lua;
//   auto locked = lua.Lock();   // the mutex is held until `locked` goes out of scope
//   locked->do_something();
//
// Keep the Locked object alive for as long as you use the value: a reference taken from a
// temporary (`lua.Lock().Get()`) outlives the lock.

#include <mutex>
#include <utility>

namespace TiltedPhoques
{
template <class T, class TMutex = std::mutex> class Lockable
{
public:
    class Locked
    {
    public:
        Locked(T& aValue, TMutex& aMutex)
            : m_pValue(&aValue)
            , m_lock(aMutex)
        {
        }

        [[nodiscard]] T& Get() const noexcept { return *m_pValue; }
        [[nodiscard]] T* operator->() const noexcept { return m_pValue; }
        [[nodiscard]] T& operator*() const noexcept { return *m_pValue; }

    private:
        T* m_pValue;
        std::unique_lock<TMutex> m_lock;
    };

    template <class... TArgs> explicit Lockable(TArgs&&... aArgs)
        : m_value(std::forward<TArgs>(aArgs)...)
    {
    }

    Lockable(const Lockable&) = delete;
    Lockable& operator=(const Lockable&) = delete;

    [[nodiscard]] Locked Lock() { return Locked(m_value, m_mutex); }

private:
    T m_value;
    TMutex m_mutex;
};
} // namespace TiltedPhoques
