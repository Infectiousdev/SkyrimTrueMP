#pragma once

// SkyrimTrueMP: carries the game's existing UDP traffic between two players over an
// IPeerLink (Steam's relay network in production).
//
//   friend's game --UDP--> TunnelClient ==link==> TunnelHost --UDP--> STServer (host's PC)
//
// The game and the server are not modified: the client points its normal connection at the
// TunnelClient's local port, and the host's TunnelHost feeds the server as if each remote
// friend were a local UDP client. The game's own protocol (handshake, encryption,
// retransmission) runs end to end through the tunnel untouched.

#include "Link.h"
#include "UdpSocket.h"

#include <functional>
#include <map>

namespace TrueMP::Steam
{
// First byte of every tunnelled datagram. Leaves room for control messages later.
enum class FrameType : uint8_t
{
    kData = 1
};

// Largest game datagram we carry. GameNetworkingSockets keeps packets near 1.2 KB.
inline constexpr size_t kMaxDatagram = 2048;

struct TunnelStats
{
    uint64_t DatagramsForwarded = 0; // accepted from one side and passed to the other
    uint64_t DatagramsDropped = 0;   // oversized, malformed, or nowhere to send
    uint64_t PeersRejected = 0;
};

// Runs on the machine that also runs the game server.
class TunnelHost final : private ILinkListener
{
public:
    // Decides whether a peer may use this server (for example "is a Steam friend").
    using AdmitFn = std::function<bool(PeerId)>;

    struct Config
    {
        uint16_t ServerPort = 10578;
        size_t MaxPeers = 8;
    };

    TunnelHost(IPeerLink& aLink, Config aConfig, AdmitFn aAdmit);

    // Call often (every frame, or from a ~1 ms thread). Never blocks.
    void Pump();

    [[nodiscard]] size_t PeerCount() const noexcept { return m_peers.size(); }
    [[nodiscard]] const TunnelStats& Stats() const noexcept { return m_stats; }

private:
    void OnPeerConnected(PeerId aPeer) override;
    void OnPeerDisconnected(PeerId aPeer) override;
    void OnDatagram(PeerId aPeer, const uint8_t* apData, size_t aSize) override;

    IPeerLink& m_link;
    Config m_config;
    AdmitFn m_admit;
    // One local UDP socket per remote player, so the server sees each as a distinct client.
    std::map<PeerId, UdpSocket> m_peers;
    TunnelStats m_stats;
};

// Runs on a friend's machine; the game connects to LocalPort() instead of the server.
class TunnelClient final : private ILinkListener
{
public:
    TunnelClient(IPeerLink& aLink, PeerId aHost);

    // Opens the local UDP port. Returns false if no loopback port could be bound.
    bool Start();
    void Pump();

    [[nodiscard]] uint16_t LocalPort() const noexcept { return m_local.LocalPort(); }
    // True once the link to the host is established; connect the game only after this.
    [[nodiscard]] bool IsLinkUp() const noexcept { return m_linkUp; }
    // True if the host closed or refused the link.
    [[nodiscard]] bool IsClosed() const noexcept { return m_closed; }
    [[nodiscard]] const TunnelStats& Stats() const noexcept { return m_stats; }

private:
    void OnPeerConnected(PeerId aPeer) override;
    void OnPeerDisconnected(PeerId aPeer) override;
    void OnDatagram(PeerId aPeer, const uint8_t* apData, size_t aSize) override;

    IPeerLink& m_link;
    PeerId m_host;
    UdpSocket m_local;
    uint16_t m_gamePort = 0; // where the game's datagrams come from; learned from its first packet
    bool m_linkUp = false;
    bool m_closed = false;
    TunnelStats m_stats;
};
} // namespace TrueMP::Steam
