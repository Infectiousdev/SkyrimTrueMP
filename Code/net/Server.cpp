#include "Server.h"

#include "Clock.h"
#include "Frame.h"
#include "Gns.h"

#include <steam/isteamnetworkingutils.h> // SteamNetworkingIPAddr::ToString

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace TrueMP::Net
{
namespace
{
constexpr int kMaxMessagesPerUpdate = 256;

// While waiting for the next tick, wake at least this often to service the network.
constexpr uint64_t kMaxSleepUs = 1000;

// If the server falls this far behind its tick schedule it skips ahead rather than running a
// burst of catch-up ticks.
constexpr uint64_t kMaxBehindUs = 1'000'000;
} // namespace

struct Server::Impl final : Gns::StatusSink
{
    struct Connection
    {
        bool Connected = false; // OnConnection() has been (or is about to be) raised
    };

    struct StatusEvent
    {
        HSteamNetConnection Connection;
        ESteamNetworkingConnectionState State;
        ESteamNetworkingConnectionState OldState;
        int EndReason;
    };

    struct DeferredDisconnect
    {
        ConnectionId_t Connection;
        Server::EDisconnectReason Reason;
    };

    explicit Impl(Server& aOwner)
        : Owner(aOwner)
    {
        Ready = Gns::Acquire();
        if (Ready)
        {
            Sockets = Gns::Sockets();
            Gns::Register(this);
        }
    }

    ~Impl() override
    {
        Shutdown();
        if (Ready)
        {
            Gns::Unregister(this);
            Gns::Release();
        }
    }

    void OnStatusChanged(const SteamNetConnectionStatusChangedCallback_t& aChange) override
    {
        // A client knocking on the listen socket: accept it now, or turn it away if we are full.
        if (aChange.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting && aChange.m_eOldState == k_ESteamNetworkingConnectionState_None && Listen != k_HSteamListenSocket_Invalid &&
            aChange.m_info.m_hListenSocket == Listen)
        {
            if (Connections.size() >= Server::kMaxConnections || Sockets->AcceptConnection(aChange.m_hConn) != k_EResultOK)
            {
                Sockets->CloseConnection(aChange.m_hConn, kEndShutdown, "server full", false);
                return;
            }

            Sockets->SetConnectionPollGroup(aChange.m_hConn, PollGroup);
            Connections[aChange.m_hConn] = Connection{};
            return;
        }

        Events.push_back({aChange.m_hConn, aChange.m_info.m_eState, aChange.m_eOldState, aChange.m_info.m_eEndReason});
    }

    void HandleEvent(const StatusEvent& aEvent)
    {
        const auto it = Connections.find(aEvent.Connection);
        if (it == Connections.end())
            return; // already removed, for example kicked

        switch (aEvent.State)
        {
        case k_ESteamNetworkingConnectionState_Connected:
            if (!it->second.Connected)
            {
                it->second.Connected = true;
                Owner.OnConnection(aEvent.Connection);
            }
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        {
            Server::EDisconnectReason reason;
            if (aEvent.State == k_ESteamNetworkingConnectionState_ClosedByPeer)
                reason = aEvent.EndReason == kEndNormal ? Server::Quit : Server::Unknown;
            else
                reason = aEvent.EndReason == k_ESteamNetConnectionEnd_Misc_Timeout ? Server::TimedOut : Server::BadConnection;

            const bool wasConnected = it->second.Connected;
            Sockets->CloseConnection(aEvent.Connection, 0, nullptr, false);
            Connections.erase(it);

            // Disconnections pair with OnConnection(); a client that never got that far is not reported.
            if (wasConnected)
                Owner.OnDisconnection(aEvent.Connection, reason);
            break;
        }

        default: break;
        }
    }

    void HandleMessage(const SteamNetworkingMessage_t& aMessage, uint64_t aNowUs)
    {
        const ConnectionId_t connection = aMessage.m_conn;
        const auto it = Connections.find(connection);
        if (it == Connections.end() || !it->second.Connected)
            return;

        DecodedFrame frame;
        if (!DecodeFrame(aMessage.m_pData, aMessage.m_cbSize, frame))
            return;

        switch (frame.Type)
        {
        case FrameType::kData:
        case FrameType::kDataCompressed:
        {
            const uint8_t* pPayload = nullptr;
            size_t payloadSize = 0;
            if (ExtractPayload(frame, RecvScratch, pPayload, payloadSize))
                Owner.OnConsume(pPayload, static_cast<uint32_t>(payloadSize), connection);
            break;
        }

        case FrameType::kClockRequest:
        {
            uint64_t clientUs = 0;
            if (!ParseClockRequest(frame, clientUs))
                return;
            const auto reply = EncodeClockReply(clientUs, aNowUs - StartUs);
            Sockets->SendMessageToConnection(connection, reply.data(), static_cast<uint32>(reply.size()), k_nSteamNetworkingSend_UnreliableNoNagle, nullptr);
            break;
        }

        default: break;
        }
    }

    // Closes everything and stops listening. Safe to call repeatedly.
    void Shutdown()
    {
        if (!Ready)
            return;

        for (const auto& [connection, info] : Connections)
            Sockets->CloseConnection(connection, kEndShutdown, "server stopping", false);
        Connections.clear();
        Events.clear();
        Deferred.clear();

        if (Listen != k_HSteamListenSocket_Invalid)
            Sockets->CloseListenSocket(Listen);
        if (PollGroup != k_HSteamNetPollGroup_Invalid)
            Sockets->DestroyPollGroup(PollGroup);
        Listen = k_HSteamListenSocket_Invalid;
        PollGroup = k_HSteamNetPollGroup_Invalid;

        if (Listening)
        {
#ifdef _WIN32
            timeEndPeriod(1);
#endif
            Listening = false;
        }
    }

    Server& Owner;
    bool Ready = false;
    bool Listening = false;
    ISteamNetworkingSockets* Sockets = nullptr;
    HSteamListenSocket Listen = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup PollGroup = k_HSteamNetPollGroup_Invalid;

    uint16_t Port = 0;
    uint32_t TickRate = 0;
    uint64_t TickPeriodUs = 0;
    uint64_t StartUs = 0;
    uint64_t NextTickUs = 0;

    std::map<HSteamNetConnection, Connection> Connections;
    std::deque<StatusEvent> Events;
    std::deque<DeferredDisconnect> Deferred;
    std::vector<uint8_t> RecvScratch;
    mutable std::vector<uint8_t> SendScratch;
};

Server::Server()
    : m_pImpl(std::make_unique<Impl>(*this))
{
}

Server::~Server() = default;

bool Server::Host(uint16_t aPort, uint32_t aTickRate, bool aEnableDualStackIP)
{
    Impl& impl = *m_pImpl;
    if (!impl.Ready || impl.Listening || aPort == 0 || aTickRate == 0)
        return false;

    SteamNetworkingIPAddr address;
    address.Clear();
    if (!aEnableDualStackIP)
        address.SetIPv4(0, aPort);
    else
        address.m_port = aPort;

    SteamNetworkingConfigValue_t options[4];
    const int count = Gns::MakeOptions(&impl, options, 0);
    impl.Listen = impl.Sockets->CreateListenSocketIP(address, count, options);
    if (impl.Listen == k_HSteamListenSocket_Invalid)
        return false;

    impl.PollGroup = impl.Sockets->CreatePollGroup();

    impl.Port = aPort;
    impl.TickRate = aTickRate;
    impl.TickPeriodUs = 1'000'000 / aTickRate;
    impl.StartUs = Clock::LocalNowUs();
    impl.NextTickUs = impl.StartUs + impl.TickPeriodUs;
    impl.Listening = true;

#ifdef _WIN32
    // The default timer tick is ~15 ms, which would make the 1 ms waits below 15 ms.
    timeBeginPeriod(1);
#endif
    return true;
}

void Server::Close()
{
    m_pImpl->Shutdown();
}

void Server::Update()
{
    Impl& impl = *m_pImpl;
    if (!impl.Listening)
        return;

    impl.Sockets->RunCallbacks();

    while (!impl.Events.empty())
    {
        const auto event = impl.Events.front();
        impl.Events.pop_front();
        impl.HandleEvent(event);
        if (!impl.Listening)
            return; // a handler called Close()
    }

    // Kicks requested since the last Update(), now that no handler is mid-flight.
    while (!impl.Deferred.empty())
    {
        const auto kick = impl.Deferred.front();
        impl.Deferred.pop_front();
        OnDisconnection(kick.Connection, kick.Reason);
        if (!impl.Listening)
            return;
    }

    const uint64_t nowUs = Clock::LocalNowUs();

    SteamNetworkingMessage_t* messages[kMaxMessagesPerUpdate];
    const int count = impl.Sockets->ReceiveMessagesOnPollGroup(impl.PollGroup, messages, kMaxMessagesPerUpdate);
    for (int i = 0; i < count; ++i)
    {
        if (impl.Listening)
            impl.HandleMessage(*messages[i], nowUs);
        messages[i]->Release();
    }
    if (!impl.Listening)
        return;

    if (nowUs >= impl.NextTickUs)
    {
        impl.NextTickUs += impl.TickPeriodUs;
        if (nowUs > impl.NextTickUs + kMaxBehindUs)
            impl.NextTickUs = nowUs + impl.TickPeriodUs;

        OnUpdate();
    }
    else if (count == 0)
    {
        // Idle: wait for the next tick, but wake often to serve the network.
        const uint64_t wait = std::min<uint64_t>(impl.NextTickUs - nowUs, kMaxSleepUs);
        std::this_thread::sleep_for(std::chrono::microseconds(wait));
    }
}

bool Server::Send(ConnectionId_t aConnectionId, const void* apData, size_t aSize, EPacketFlags aFlags) const
{
    Impl& impl = *m_pImpl;
    const auto it = impl.Connections.find(aConnectionId);
    if (!impl.Listening || it == impl.Connections.end() || !it->second.Connected)
        return false;

    if (!EncodePayload(apData, aSize, impl.SendScratch))
        return false;

    const int flags = aFlags == kReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    return impl.Sockets->SendMessageToConnection(aConnectionId, impl.SendScratch.data(), static_cast<uint32>(impl.SendScratch.size()), flags, nullptr) == k_EResultOK;
}

bool Server::SendToAll(const void* apData, size_t aSize, EPacketFlags aFlags) const
{
    Impl& impl = *m_pImpl;
    if (!impl.Listening || !EncodePayload(apData, aSize, impl.SendScratch))
        return false;

    // Compressed once, sent to everyone.
    const int flags = aFlags == kReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    for (const auto& [connection, info] : impl.Connections)
    {
        if (info.Connected)
            impl.Sockets->SendMessageToConnection(connection, impl.SendScratch.data(), static_cast<uint32>(impl.SendScratch.size()), flags, nullptr);
    }
    return true;
}

void Server::Kick(ConnectionId_t aConnectionId)
{
    Impl& impl = *m_pImpl;
    const auto it = impl.Connections.find(aConnectionId);
    if (!impl.Listening || it == impl.Connections.end())
        return;

    const bool wasConnected = it->second.Connected;

    // Linger: whatever was queued to the client (often the reason it is being kicked) goes out first.
    impl.Sockets->CloseConnection(aConnectionId, kEndKicked, "kicked", true);
    impl.Connections.erase(it);

    if (wasConnected)
        impl.Deferred.push_back({aConnectionId, Server::Kicked});
}

uint16_t Server::GetPort() const noexcept
{
    return m_pImpl->Port;
}

bool Server::IsListening() const noexcept
{
    return m_pImpl->Listening;
}

uint32_t Server::GetClientCount() const noexcept
{
    uint32_t count = 0;
    for (const auto& [connection, info] : m_pImpl->Connections)
        count += info.Connected ? 1 : 0;
    return count;
}

uint32_t Server::GetTickRate() const noexcept
{
    return m_pImpl->TickRate;
}

uint64_t Server::GetTick() const noexcept
{
    return m_pImpl->StartUs == 0 ? 0 : (Clock::LocalNowUs() - m_pImpl->StartUs) / 1000;
}

std::string Server::GetRemoteAddress(ConnectionId_t aConnectionId) const
{
    SteamNetConnectionInfo_t info;
    if (!m_pImpl->Ready || !m_pImpl->Sockets->GetConnectionInfo(aConnectionId, &info))
        return {};

    char text[SteamNetworkingIPAddr::k_cchMaxString] = {};
    info.m_addrRemote.ToString(text, sizeof(text), false);
    return text;
}

bool Server::IsAlive(ConnectionId_t aConnectionId) const
{
    return m_pImpl->Connections.count(aConnectionId) != 0;
}
} // namespace TrueMP::Net
