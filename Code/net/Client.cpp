#include "Client.h"

#include "Frame.h"
#include "Gns.h"
#include "Resolve.h"

#include <deque>
#include <vector>

namespace TrueMP::Net
{
namespace
{
constexpr uint64_t kSecondUs = 1000 * 1000;

// Clock requests start dense so the client is synchronised within a fraction of a second, then
// settle to one every few seconds to track drift.
constexpr uint64_t kClockScheduleUs[] = {0, 100'000, 250'000, 500'000, 1'000'000, 2'000'000};
constexpr uint64_t kClockSteadyUs = 5 * kSecondUs;

constexpr int kMaxMessagesPerUpdate = 64;
} // namespace

struct Client::Impl final : Gns::StatusSink
{
    enum class Phase
    {
        kIdle,
        kResolving,
        kConnecting,
        kLinkUp, // connected, waiting for the first clock reply
        kReady
    };

    struct StatusEvent
    {
        HSteamNetConnection Connection;
        ESteamNetworkingConnectionState State;
        ESteamNetworkingConnectionState OldState;
        int EndReason;
    };

    explicit Impl(Client& aOwner)
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
        if (Ready)
        {
            Gns::Unregister(this);
            if (Connection != k_HSteamNetConnection_Invalid)
                Sockets->CloseConnection(Connection, kEndNormal, "client destroyed", false);
            Gns::Release();
        }
    }

    void OnStatusChanged(const SteamNetConnectionStatusChangedCallback_t& aChange) override
    {
        // Queued and handled in Update(), so callbacks to the game never run inside the library.
        Events.push_back({aChange.m_hConn, aChange.m_info.m_eState, aChange.m_eOldState, aChange.m_info.m_eEndReason});
    }

    void OpenConnection(const Address& aAddress)
    {
        SteamNetworkingIPAddr address;
        address.Clear();
        if (aAddress.IsV6)
            address.SetIPv6(aAddress.Bytes, aAddress.Port);
        else
            address.SetIPv4((uint32(aAddress.Bytes[0]) << 24) | (uint32(aAddress.Bytes[1]) << 16) | (uint32(aAddress.Bytes[2]) << 8) | uint32(aAddress.Bytes[3]), aAddress.Port);

        SteamNetworkingConfigValue_t options[4];
        const int count = Gns::MakeOptions(this, options, ConnectTimeoutMs);
        Connection = Sockets->ConnectByIPAddress(address, count, options);

        if (Connection == k_HSteamNetConnection_Invalid)
        {
            Mode = Phase::kIdle;
            Owner.OnDisconnected(kLocalProblem);
            return;
        }
        Mode = Phase::kConnecting;
    }

    // Forget the connection without telling the owner. Used on every path that ends one.
    void Reset()
    {
        if (Connection != k_HSteamNetConnection_Invalid)
            Sockets->CloseConnection(Connection, kEndNormal, "closed", true);
        Connection = k_HSteamNetConnection_Invalid;
        Mode = Phase::kIdle;
        Clk.Reset();
        Resolve.reset();
        Events.clear();
        ClockRequestsSent = 0;
    }

    void SendClockRequest(uint64_t aNowUs)
    {
        const auto frame = EncodeClockRequest(aNowUs);
        Sockets->SendMessageToConnection(Connection, frame.data(), static_cast<uint32>(frame.size()), k_nSteamNetworkingSend_UnreliableNoNagle, nullptr);

        constexpr size_t kScheduleCount = sizeof(kClockScheduleUs) / sizeof(kClockScheduleUs[0]);
        ++ClockRequestsSent;
        NextClockRequestUs = aNowUs + (ClockRequestsSent < kScheduleCount ? kClockScheduleUs[ClockRequestsSent] : kClockSteadyUs);
    }

    void HandleEvent(const StatusEvent& aEvent)
    {
        if (aEvent.Connection != Connection)
            return; // a connection we already let go of

        switch (aEvent.State)
        {
        case k_ESteamNetworkingConnectionState_Connected:
            if (Mode == Phase::kConnecting)
            {
                Mode = Phase::kLinkUp;
                Clk.Reset();
                ClockRequestsSent = 0;
                SendClockRequest(Clock::LocalNowUs());
            }
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        {
            EDisconnectReason reason;
            if (aEvent.State == k_ESteamNetworkingConnectionState_ClosedByPeer)
                reason = (aEvent.OldState == k_ESteamNetworkingConnectionState_Connecting) ? kTimeout : kKicked;
            else if (aEvent.OldState == k_ESteamNetworkingConnectionState_Connecting || aEvent.EndReason == k_ESteamNetConnectionEnd_Misc_Timeout)
                reason = kTimeout;
            else
                reason = kLocalProblem;

            // The library keeps the handle until we release it.
            Sockets->CloseConnection(Connection, 0, nullptr, false);
            Connection = k_HSteamNetConnection_Invalid;
            Mode = Phase::kIdle;
            Clk.Reset();
            Owner.OnDisconnected(reason);
            break;
        }

        default: break;
        }
    }

    void HandleMessage(const SteamNetworkingMessage_t& aMessage, uint64_t aNowUs)
    {
        Current.RecvBytes += aMessage.m_cbSize;

        DecodedFrame frame;
        if (!DecodeFrame(aMessage.m_pData, aMessage.m_cbSize, frame))
            return; // not ours; ignore rather than trust it

        switch (frame.Type)
        {
        case FrameType::kData:
        case FrameType::kDataCompressed:
        {
            const uint8_t* pPayload = nullptr;
            size_t payloadSize = 0;
            if (!ExtractPayload(frame, RecvScratch, pPayload, payloadSize))
                return;
            Current.UncompressedRecvBytes += static_cast<uint32_t>(payloadSize);
            Owner.OnConsume(pPayload, static_cast<uint32_t>(payloadSize));
            break;
        }

        case FrameType::kClockReply:
        {
            uint64_t sentUs = 0, serverUs = 0;
            if (!ParseClockReply(frame, sentUs, serverUs))
                return;
            Clk.AddSample(sentUs, serverUs, aNowUs);

            // The first reply completes the connection: from here the game may rely on the clock.
            if (Mode == Phase::kLinkUp && Clk.IsSynchronized())
            {
                Mode = Phase::kReady;
                Owner.OnConnected();
            }
            break;
        }

        default: break;
        }
    }

    Client& Owner;
    bool Ready = false;
    ISteamNetworkingSockets* Sockets = nullptr;

    HSteamNetConnection Connection = k_HSteamNetConnection_Invalid;
    Phase Mode = Phase::kIdle;
    std::shared_ptr<ResolveJob> Resolve;
    int ConnectTimeoutMs = 0;

    Clock Clk;
    uint64_t NextClockRequestUs = 0;
    size_t ClockRequestsSent = 0;

    std::deque<StatusEvent> Events;

    Statistics Current;
    Statistics Previous;
    uint64_t WindowStartUs = 0;

    std::vector<uint8_t> SendScratch; // reused between sends
    std::vector<uint8_t> RecvScratch;
};

Client::Client()
    : m_pImpl(std::make_unique<Impl>(*this))
{
}

Client::~Client() = default;

bool Client::Connect(const std::string& aEndpoint)
{
    if (!m_pImpl->Ready)
        return false;

    std::string host;
    uint16_t port = 0;
    if (!SplitEndpoint(aEndpoint, kDefaultPort, host, port))
        return false;

    // Starting again abandons whatever was in progress, without reporting it as a failure.
    m_pImpl->Reset();
    m_pImpl->Mode = Impl::Phase::kResolving;
    m_pImpl->Resolve = ResolveJob::Start(std::move(host), port);
    return true;
}

void Client::Close()
{
    if (m_pImpl->Mode == Impl::Phase::kIdle)
        return;

    m_pImpl->Reset();
    OnDisconnected(kAborted);
}

void Client::Update()
{
    Impl& impl = *m_pImpl;
    if (!impl.Ready)
    {
        OnUpdate();
        return;
    }

    const uint64_t nowUs = Clock::LocalNowUs();
    if (impl.WindowStartUs == 0)
        impl.WindowStartUs = nowUs;
    if (nowUs - impl.WindowStartUs >= kSecondUs)
    {
        impl.Previous = impl.Current;
        impl.Current = {};
        impl.WindowStartUs = nowUs;
    }

    // A lookup that has finished becomes a connection attempt.
    if (impl.Mode == Impl::Phase::kResolving && impl.Resolve && impl.Resolve->Done())
    {
        const Address address = impl.Resolve->Result();
        impl.Resolve.reset();
        if (!address.Valid)
        {
            impl.Mode = Impl::Phase::kIdle;
            OnDisconnected(kCannotResolve);
        }
        else
        {
            impl.OpenConnection(address);
        }
    }

    impl.Sockets->RunCallbacks();

    while (!impl.Events.empty())
    {
        const auto event = impl.Events.front();
        impl.Events.pop_front();
        impl.HandleEvent(event);
    }

    if (impl.Connection != k_HSteamNetConnection_Invalid)
    {
        SteamNetworkingMessage_t* messages[kMaxMessagesPerUpdate];
        const int count = impl.Sockets->ReceiveMessagesOnConnection(impl.Connection, messages, kMaxMessagesPerUpdate);
        for (int i = 0; i < count; ++i)
        {
            // A handler may have closed the connection; the rest of the batch is then dropped.
            if (impl.Connection != k_HSteamNetConnection_Invalid)
                impl.HandleMessage(*messages[i], nowUs);
            messages[i]->Release();
        }

        if ((impl.Mode == Impl::Phase::kLinkUp || impl.Mode == Impl::Phase::kReady) && impl.Connection != k_HSteamNetConnection_Invalid && nowUs >= impl.NextClockRequestUs)
            impl.SendClockRequest(nowUs);
    }

    OnUpdate();
}

bool Client::Send(const void* apData, size_t aSize, EPacketFlags aFlags) const
{
    Impl& impl = *m_pImpl;
    if (impl.Connection == k_HSteamNetConnection_Invalid || (impl.Mode != Impl::Phase::kReady && impl.Mode != Impl::Phase::kLinkUp))
        return false;

    if (!EncodePayload(apData, aSize, impl.SendScratch))
        return false;

    impl.Current.UncompressedSentBytes += static_cast<uint32_t>(aSize);
    impl.Current.SentBytes += static_cast<uint32_t>(impl.SendScratch.size());

    const int flags = aFlags == kReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    return impl.Sockets->SendMessageToConnection(impl.Connection, impl.SendScratch.data(), static_cast<uint32>(impl.SendScratch.size()), flags, nullptr) == k_EResultOK;
}

bool Client::IsConnected() const noexcept
{
    return m_pImpl->Mode == Impl::Phase::kReady;
}

Client::ConnectionStatus Client::GetConnectionStatus() const
{
    ConnectionStatus status;
    if (m_pImpl->Connection == k_HSteamNetConnection_Invalid)
        return status;

    SteamNetConnectionRealTimeStatus_t raw{};
    if (m_pImpl->Sockets->GetConnectionRealTimeStatus(m_pImpl->Connection, &raw, 0, nullptr) != k_EResultOK)
        return status;

    status.OutPacketsPerSec = raw.m_flOutPacketsPerSec;
    status.InPacketsPerSec = raw.m_flInPacketsPerSec;
    status.OutBytesPerSec = raw.m_flOutBytesPerSec;
    status.InBytesPerSec = raw.m_flInBytesPerSec;
    status.PingMs = raw.m_nPing;
    status.QualityLocal = raw.m_flConnectionQualityLocal;
    return status;
}

Client::Statistics Client::GetStatistics() const
{
    return m_pImpl->Previous;
}

const Clock& Client::GetClock() const noexcept
{
    return m_pImpl->Clk;
}

void Client::SetConnectTimeout(int aMilliseconds) noexcept
{
    m_pImpl->ConnectTimeoutMs = aMilliseconds;
}
} // namespace TrueMP::Net
