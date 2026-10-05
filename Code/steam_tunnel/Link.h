#pragma once

// SkyrimTrueMP: the seam between the tunnel and whatever carries datagrams between two
// players. The tunnel core only knows this interface; the real implementation sits on
// Steam's relay network (SteamP2PLink) and tests use an in-process LoopbackLink.

#include <cstddef>
#include <cstdint>

namespace TrueMP::Steam
{
// A SteamID64 for the real link; any unique number for test links.
using PeerId = uint64_t;

// Receives what happens on a link. Called from IPeerLink::Poll() only.
struct ILinkListener
{
    virtual ~ILinkListener() = default;

    virtual void OnPeerConnected(PeerId aPeer) = 0;
    virtual void OnPeerDisconnected(PeerId aPeer) = 0;
    virtual void OnDatagram(PeerId aPeer, const uint8_t* apData, size_t aSize) = 0;
};

struct IPeerLink
{
    virtual ~IPeerLink() = default;

    // Pumps the link and dispatches pending events to the listener. Never blocks.
    virtual void Poll(ILinkListener& aListener) = 0;

    // Sends one datagram, unreliably: the game's own network layer already retransmits, and
    // doing it twice would only add head-of-line blocking. Returns false if the peer is gone.
    virtual bool Send(PeerId aPeer, const uint8_t* apData, size_t aSize) = 0;

    virtual void Disconnect(PeerId aPeer) = 0;
};
} // namespace TrueMP::Steam
