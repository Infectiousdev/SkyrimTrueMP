#pragma once

// SkyrimTrueMP: an in-process IPeerLink used by tests and the end-to-end probe. It behaves
// like a link to remote peers but moves datagrams through memory.

#include "Link.h"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace TrueMP::Steam
{
class LoopbackNetwork
{
public:
    LoopbackNetwork();
    ~LoopbackNetwork();

    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

    // The link the host side listens on. It sees OnPeerConnected for every Connect() below.
    IPeerLink& Host();

    // Creates a client link (its own PeerId is aClient) connected to the host.
    std::unique_ptr<IPeerLink> Connect(PeerId aClient);

    // The id the host appears as to clients.
    static constexpr PeerId kHostId = 1;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace TrueMP::Steam
