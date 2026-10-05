#pragma once

// SkyrimTrueMP: an IPeerLink on top of any ISteamNetworkingSockets implementation.
//
// In the game this is Steam's own implementation (relay network, identity = SteamID64,
// no port forwarding). Tests use the standalone GameNetworkingSockets library over
// loopback IP. Everything in this class is identical in both cases except the two calls
// that open a connection (ListenP2P/ConnectP2P versus ListenIp/ConnectIp).

#include "Link.h"

#include <steam/steamnetworkingsockets.h>

#include <deque>
#include <functional>
#include <map>

namespace TrueMP::Steam
{
class GnsLink final : public IPeerLink
{
public:
    // Host only: decides which remote peers may connect. Called before the connection is accepted.
    using AdmitFn = std::function<bool(PeerId)>;

    // aSockets must outlive the link.
    explicit GnsLink(ISteamNetworkingSockets* apSockets, AdmitFn aAdmit = {});
    ~GnsLink() override;

    GnsLink(const GnsLink&) = delete;
    GnsLink& operator=(const GnsLink&) = delete;

    // Host: accept peers.
    bool ListenP2P(int aVirtualPort);           // Steam relay; peers are identified by SteamID64
    bool ListenIp(uint16_t aPort);              // aPort must be non-zero: the library cannot pick one
    [[nodiscard]] uint16_t ListenPort() const;  // the port the host is listening on, 0 if not listening

    // Client: connect to the host. The host is reported to the listener as aHostLabel.
    bool ConnectP2P(PeerId aHostSteamId, int aVirtualPort);
    bool ConnectIp(const SteamNetworkingIPAddr& aAddress, PeerId aHostLabel);

    void Poll(ILinkListener& aListener) override;
    bool Send(PeerId aPeer, const uint8_t* apData, size_t aSize) override;
    void Disconnect(PeerId aPeer) override;

    // Connections this link refused because the admit function said no.
    [[nodiscard]] size_t RejectedCount() const noexcept { return m_rejected; }

    // Reason code sent to a peer that is turned away.
    static constexpr int kEndNotAllowed = k_ESteamNetConnectionEnd_App_Min + 1;

private:
    struct Connection
    {
        PeerId Peer = 0;
        bool Outgoing = false;
        bool Notified = false; // OnPeerConnected has been (or is about to be) raised
    };

    struct Event
    {
        enum class Kind
        {
            kConnected,
            kDisconnected
        } Type;
        PeerId Peer;
    };

    static void StatusChangedTrampoline(SteamNetConnectionStatusChangedCallback_t* apInfo);
    void OnStatusChanged(const SteamNetConnectionStatusChangedCallback_t& acInfo);

    void FillOptions(SteamNetworkingConfigValue_t (&aOptions)[2]);
    [[nodiscard]] static PeerId PeerIdOf(const SteamNetConnectionInfo_t& acInfo, HSteamNetConnection aConnection) noexcept;
    void Forget(HSteamNetConnection aConnection);

    ISteamNetworkingSockets* m_pSockets;
    AdmitFn m_admit;
    HSteamListenSocket m_listen = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup m_pollGroup = k_HSteamNetPollGroup_Invalid;

    std::map<HSteamNetConnection, Connection> m_connections;
    std::map<PeerId, HSteamNetConnection> m_byPeer;
    std::deque<Event> m_events;
    size_t m_rejected = 0;
};
} // namespace TrueMP::Steam
