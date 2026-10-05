#include "Tunnel.h"

#include <array>

namespace TrueMP::Steam
{
namespace
{
// Bounds the work done per Pump() so a flood cannot starve the game thread.
constexpr int kMaxDatagramsPerPump = 512;

// Wraps a game datagram into a link frame.
size_t Frame(const uint8_t* apPayload, size_t aSize, std::array<uint8_t, kMaxDatagram + 1>& aFrame)
{
    aFrame[0] = static_cast<uint8_t>(FrameType::kData);
    std::copy(apPayload, apPayload + aSize, aFrame.begin() + 1);
    return aSize + 1;
}
} // namespace

// ---------------------------------------------------------------- TunnelHost

TunnelHost::TunnelHost(IPeerLink& aLink, Config aConfig, AdmitFn aAdmit)
    : m_link(aLink)
    , m_config(aConfig)
    , m_admit(std::move(aAdmit))
{
}

void TunnelHost::Pump()
{
    m_link.Poll(*this);

    std::array<uint8_t, kMaxDatagram + 1> frame{};
    std::array<uint8_t, kMaxDatagram + 1> buffer{};

    // Server -> remote players.
    for (auto& [peer, socket] : m_peers)
    {
        for (int i = 0; i < kMaxDatagramsPerPump; ++i)
        {
            const int received = socket.Receive(buffer.data(), buffer.size());
            if (received <= 0)
                break;

            if (static_cast<size_t>(received) > kMaxDatagram)
            {
                ++m_stats.DatagramsDropped;
                continue;
            }

            const size_t frameSize = Frame(buffer.data(), static_cast<size_t>(received), frame);
            if (m_link.Send(peer, frame.data(), frameSize))
                ++m_stats.DatagramsForwarded;
            else
                ++m_stats.DatagramsDropped;
        }
    }
}

void TunnelHost::OnPeerConnected(PeerId aPeer)
{
    // The link may already have vetted the peer; this is the second check, so a link that
    // forgets to filter still cannot expose the server to strangers.
    if (m_peers.size() >= m_config.MaxPeers || !m_admit || !m_admit(aPeer))
    {
        ++m_stats.PeersRejected;
        m_link.Disconnect(aPeer);
        return;
    }

    UdpSocket socket;
    if (!socket.ConnectLoopback(m_config.ServerPort))
    {
        ++m_stats.PeersRejected;
        m_link.Disconnect(aPeer);
        return;
    }

    m_peers.emplace(aPeer, std::move(socket));
}

void TunnelHost::OnPeerDisconnected(PeerId aPeer)
{
    m_peers.erase(aPeer);
}

void TunnelHost::OnDatagram(PeerId aPeer, const uint8_t* apData, size_t aSize)
{
    // Only peers that were admitted have a socket; everything else is dropped.
    const auto it = m_peers.find(aPeer);
    if (it == m_peers.end() || aSize < 2 || aSize > kMaxDatagram + 1 || apData[0] != static_cast<uint8_t>(FrameType::kData))
    {
        ++m_stats.DatagramsDropped;
        return;
    }

    if (it->second.Send(apData + 1, aSize - 1))
        ++m_stats.DatagramsForwarded;
    else
        ++m_stats.DatagramsDropped;
}

// -------------------------------------------------------------- TunnelClient

TunnelClient::TunnelClient(IPeerLink& aLink, PeerId aHost)
    : m_link(aLink)
    , m_host(aHost)
{
}

bool TunnelClient::Start()
{
    return m_local.BindLoopback(0);
}

void TunnelClient::Pump()
{
    m_link.Poll(*this);

    if (!m_local.IsOpen())
        return;

    std::array<uint8_t, kMaxDatagram + 1> frame{};
    std::array<uint8_t, kMaxDatagram + 1> buffer{};

    // Game -> host.
    for (int i = 0; i < kMaxDatagramsPerPump; ++i)
    {
        uint16_t fromPort = 0;
        const int received = m_local.Receive(buffer.data(), buffer.size(), &fromPort);
        if (received <= 0)
            break;

        // Remember where the game talks from so replies can be sent back to it.
        m_gamePort = fromPort;

        if (static_cast<size_t>(received) > kMaxDatagram || !m_linkUp)
        {
            // Dropped before the link is up; the game's handshake retries on its own.
            ++m_stats.DatagramsDropped;
            continue;
        }

        const size_t frameSize = Frame(buffer.data(), static_cast<size_t>(received), frame);
        if (m_link.Send(m_host, frame.data(), frameSize))
            ++m_stats.DatagramsForwarded;
        else
            ++m_stats.DatagramsDropped;
    }
}

void TunnelClient::OnPeerConnected(PeerId aPeer)
{
    if (aPeer == m_host)
        m_linkUp = true;
}

void TunnelClient::OnPeerDisconnected(PeerId aPeer)
{
    if (aPeer == m_host)
    {
        m_linkUp = false;
        m_closed = true;
    }
}

void TunnelClient::OnDatagram(PeerId aPeer, const uint8_t* apData, size_t aSize)
{
    // Only the host may talk to us, and only once the game has told us where it listens.
    if (aPeer != m_host || m_gamePort == 0 || aSize < 2 || aSize > kMaxDatagram + 1 || apData[0] != static_cast<uint8_t>(FrameType::kData))
    {
        ++m_stats.DatagramsDropped;
        return;
    }

    if (m_local.SendTo(apData + 1, aSize - 1, m_gamePort))
        ++m_stats.DatagramsForwarded;
    else
        ++m_stats.DatagramsDropped;
}
} // namespace TrueMP::Steam
