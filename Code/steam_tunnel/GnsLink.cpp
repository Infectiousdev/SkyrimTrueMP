#include "GnsLink.h"

#include <mutex>
#include <set>
#include <vector>

namespace TrueMP::Steam
{
namespace
{
// Status callbacks are delivered by RunCallbacks() for every connection on the interface,
// not just ours, so each connection carries a pointer to its owning link as user data and
// the link must still exist when the callback arrives.
std::mutex g_registryMutex;
std::set<const void*> g_liveLinks;

constexpr int kMaxMessagesPerPoll = 64;
constexpr PeerId kSyntheticPeerBit = PeerId(1) << 63;
} // namespace

GnsLink::GnsLink(ISteamNetworkingSockets* apSockets, AdmitFn aAdmit)
    : m_pSockets(apSockets)
    , m_admit(std::move(aAdmit))
{
    m_pollGroup = m_pSockets->CreatePollGroup();

    std::lock_guard lock(g_registryMutex);
    g_liveLinks.insert(this);
}

GnsLink::~GnsLink()
{
    {
        std::lock_guard lock(g_registryMutex);
        g_liveLinks.erase(this);
    }

    for (const auto& [connection, info] : m_connections)
        m_pSockets->CloseConnection(connection, 0, "link closed", false);
    if (m_listen != k_HSteamListenSocket_Invalid)
        m_pSockets->CloseListenSocket(m_listen);
    if (m_pollGroup != k_HSteamNetPollGroup_Invalid)
        m_pSockets->DestroyPollGroup(m_pollGroup);
}

void GnsLink::FillOptions(SteamNetworkingConfigValue_t (&aOptions)[2])
{
    aOptions[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&GnsLink::StatusChangedTrampoline));
    aOptions[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, reinterpret_cast<int64>(this));
}

bool GnsLink::ListenP2P(int aVirtualPort)
{
    // Fail closed: a host link with no admission policy would let anyone in.
    if (!m_admit)
        return false;

    SteamNetworkingConfigValue_t options[2];
    FillOptions(options);
    m_listen = m_pSockets->CreateListenSocketP2P(aVirtualPort, 2, options);
    return m_listen != k_HSteamListenSocket_Invalid;
}

bool GnsLink::ListenIp(uint16_t aPort)
{
    if (!m_admit || aPort == 0)
        return false;

    SteamNetworkingIPAddr address;
    address.Clear();
    address.m_port = aPort;

    SteamNetworkingConfigValue_t options[2];
    FillOptions(options);
    m_listen = m_pSockets->CreateListenSocketIP(address, 2, options);
    return m_listen != k_HSteamListenSocket_Invalid;
}

uint16_t GnsLink::ListenPort() const
{
    SteamNetworkingIPAddr address;
    if (m_listen == k_HSteamListenSocket_Invalid || !m_pSockets->GetListenSocketAddress(m_listen, &address))
        return 0;
    return address.m_port;
}

bool GnsLink::ConnectP2P(PeerId aHostSteamId, int aVirtualPort)
{
    SteamNetworkingIdentity identity;
    identity.Clear();
    identity.SetSteamID64(aHostSteamId);

    SteamNetworkingConfigValue_t options[2];
    FillOptions(options);
    const HSteamNetConnection connection = m_pSockets->ConnectP2P(identity, aVirtualPort, 2, options);
    if (connection == k_HSteamNetConnection_Invalid)
        return false;

    m_connections[connection] = Connection{aHostSteamId, true, false};
    m_byPeer[aHostSteamId] = connection;
    m_pSockets->SetConnectionPollGroup(connection, m_pollGroup);
    return true;
}

bool GnsLink::ConnectIp(const SteamNetworkingIPAddr& aAddress, PeerId aHostLabel)
{
    SteamNetworkingConfigValue_t options[2];
    FillOptions(options);
    const HSteamNetConnection connection = m_pSockets->ConnectByIPAddress(aAddress, 2, options);
    if (connection == k_HSteamNetConnection_Invalid)
        return false;

    m_connections[connection] = Connection{aHostLabel, true, false};
    m_byPeer[aHostLabel] = connection;
    m_pSockets->SetConnectionPollGroup(connection, m_pollGroup);
    return true;
}

PeerId GnsLink::PeerIdOf(const SteamNetConnectionInfo_t& acInfo, HSteamNetConnection aConnection) noexcept
{
    // Over Steam the remote is always a SteamID64. Plain IP connections (tests) have none, so
    // they get an id that cannot collide with a real SteamID (those never set the top bit).
    const uint64 steamId = acInfo.m_identityRemote.GetSteamID64();
    return steamId != 0 ? steamId : (kSyntheticPeerBit | aConnection);
}

void GnsLink::StatusChangedTrampoline(SteamNetConnectionStatusChangedCallback_t* apInfo)
{
    if (!apInfo)
        return;

    const auto* pOwner = reinterpret_cast<GnsLink*>(apInfo->m_info.m_nUserData);

    std::lock_guard lock(g_registryMutex);
    if (g_liveLinks.count(pOwner))
        const_cast<GnsLink*>(pOwner)->OnStatusChanged(*apInfo);
}

void GnsLink::OnStatusChanged(const SteamNetConnectionStatusChangedCallback_t& acInfo)
{
    const HSteamNetConnection connection = acInfo.m_hConn;
    const ESteamNetworkingConnectionState state = acInfo.m_info.m_eState;

    // A remote peer is knocking on our listen socket.
    if (state == k_ESteamNetworkingConnectionState_Connecting && acInfo.m_eOldState == k_ESteamNetworkingConnectionState_None && m_listen != k_HSteamListenSocket_Invalid &&
        acInfo.m_info.m_hListenSocket == m_listen)
    {
        const PeerId peer = PeerIdOf(acInfo.m_info, connection);

        if (!m_admit || !m_admit(peer))
        {
            ++m_rejected;
            m_pSockets->CloseConnection(connection, kEndNotAllowed, "not allowed", false);
            return;
        }

        // The same player reconnecting (a restarted game, say) replaces their stale connection
        // rather than being locked out until the old one times out.
        if (const auto stale = m_byPeer.find(peer); stale != m_byPeer.end())
        {
            m_pSockets->CloseConnection(stale->second, 0, "replaced", false);
            if (m_connections[stale->second].Notified)
                m_events.push_back({Event::Kind::kDisconnected, peer});
            Forget(stale->second);
        }

        if (m_pSockets->AcceptConnection(connection) != k_EResultOK)
        {
            m_pSockets->CloseConnection(connection, 0, "accept failed", false);
            return;
        }

        m_pSockets->SetConnectionPollGroup(connection, m_pollGroup);
        m_connections[connection] = Connection{peer, false, false};
        m_byPeer[peer] = connection;
        return;
    }

    const auto it = m_connections.find(connection);
    if (it == m_connections.end())
        return;

    switch (state)
    {
    case k_ESteamNetworkingConnectionState_Connected:
        if (!it->second.Notified)
        {
            it->second.Notified = true;
            m_events.push_back({Event::Kind::kConnected, it->second.Peer});
        }
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        m_pSockets->CloseConnection(connection, 0, nullptr, false);

        // An outgoing attempt that never connected still has to be reported, or the caller would
        // wait on it forever.
        if (it->second.Notified || it->second.Outgoing)
            m_events.push_back({Event::Kind::kDisconnected, it->second.Peer});
        Forget(connection);
        break;

    default: break;
    }
}

void GnsLink::Forget(HSteamNetConnection aConnection)
{
    const auto it = m_connections.find(aConnection);
    if (it == m_connections.end())
        return;
    m_byPeer.erase(it->second.Peer);
    m_connections.erase(it);
}

void GnsLink::Poll(ILinkListener& aListener)
{
    m_pSockets->RunCallbacks();

    // Take the queue first: a listener may call Disconnect() or Send() while we dispatch.
    std::deque<Event> events;
    events.swap(m_events);
    for (const auto& event : events)
    {
        if (event.Type == Event::Kind::kConnected)
            aListener.OnPeerConnected(event.Peer);
        else
            aListener.OnPeerDisconnected(event.Peer);
    }

    SteamNetworkingMessage_t* messages[kMaxMessagesPerPoll];
    const int count = m_pSockets->ReceiveMessagesOnPollGroup(m_pollGroup, messages, kMaxMessagesPerPoll);
    for (int i = 0; i < count; ++i)
    {
        const auto it = m_connections.find(messages[i]->m_conn);
        if (it != m_connections.end() && it->second.Notified)
            aListener.OnDatagram(it->second.Peer, static_cast<const uint8_t*>(messages[i]->m_pData), static_cast<size_t>(messages[i]->m_cbSize));
        messages[i]->Release();
    }
}

bool GnsLink::Send(PeerId aPeer, const uint8_t* apData, size_t aSize)
{
    const auto it = m_byPeer.find(aPeer);
    if (it == m_byPeer.end())
        return false;

    // Unreliable and without Nagle: the game's own layer retransmits, and latency matters more
    // than packing.
    return m_pSockets->SendMessageToConnection(it->second, apData, static_cast<uint32>(aSize), k_nSteamNetworkingSend_UnreliableNoNagle, nullptr) == k_EResultOK;
}

void GnsLink::Disconnect(PeerId aPeer)
{
    const auto it = m_byPeer.find(aPeer);
    if (it == m_byPeer.end())
        return;

    const HSteamNetConnection connection = it->second;
    m_pSockets->CloseConnection(connection, kEndNotAllowed, "disconnected", false);
    Forget(connection);
}
} // namespace TrueMP::Steam
