#pragma once

// An event with any number of listeners.
//
//   Signal<void(int)> onChanged;
//   auto id = onChanged.Connect([](int value) { ... });
//   onChanged.Emit(5);        // or onChanged(5)
//   onChanged.Disconnect(id);
//
// Listeners may connect or disconnect other listeners (or themselves) while the signal is emitting;
// the change takes effect from the next emit.

#include "Meta.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace TiltedPhoques
{
template <class TSignature> class Signal;

template <class... TArgs> class Signal<void(TArgs...)>
{
public:
    using Listener = std::function<void(TArgs...)>;
    using ConnectionId = uint64_t;

    Signal() = default;
    TP_NOCOPYMOVE(Signal);

    ConnectionId Connect(Listener aListener)
    {
        const ConnectionId id = ++m_lastId;
        m_listeners.push_back({id, std::move(aListener)});
        return id;
    }

    void Disconnect(ConnectionId aId)
    {
        m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [aId](const Entry& aEntry) { return aEntry.Id == aId; }), m_listeners.end());
    }

    void Emit(TArgs... aArgs) const
    {
        // Iterate a copy so listeners can change the list.
        const auto snapshot = m_listeners;
        for (const auto& entry : snapshot)
            entry.Function(aArgs...);
    }

    void operator()(TArgs... aArgs) const { Emit(aArgs...); }

    [[nodiscard]] size_t ListenerCount() const noexcept { return m_listeners.size(); }

private:
    struct Entry
    {
        ConnectionId Id;
        Listener Function;
    };

    std::vector<Entry> m_listeners;
    ConnectionId m_lastId = 0;
};
} // namespace TiltedPhoques
