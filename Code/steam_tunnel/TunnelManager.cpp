#include "TunnelManager.h"

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace TrueMP::Steam
{
bool AdmissionPolicy::operator()(PeerId aPeer) const
{
    if (AllowList.count(aPeer))
        return true;

    // If Steam cannot tell us, nobody but the allow-list gets in.
    const std::optional<bool> friendship = IsFriend ? IsFriend(aPeer) : std::nullopt;
    return friendship.value_or(false);
}

TunnelManager::TunnelManager(Environment aEnvironment)
    : m_env(std::move(aEnvironment))
{
}

TunnelManager::~TunnelManager()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

void TunnelManager::EnsureThread()
{
    if (!m_thread.joinable())
        m_thread = std::thread([this] { Run(); });
}

bool TunnelManager::StartHosting(uint16_t aServerPort)
{
    if (!m_env.Sockets || !m_env.Listen)
        return false;

    auto hosting = std::make_unique<Hosting>();
    hosting->Link = std::make_unique<GnsLink>(m_env.Sockets, m_env.Admission);
    if (!m_env.Listen(*hosting->Link))
        return false;
    hosting->Tunnel = std::make_unique<TunnelHost>(*hosting->Link, TunnelHost::Config{aServerPort, 8}, m_env.Admission);

    {
        std::lock_guard lock(m_mutex);
        m_hosting = std::move(hosting);
    }
    EnsureThread();
    return true;
}

void TunnelManager::StopHosting()
{
    std::lock_guard lock(m_mutex);
    m_hosting.reset();
}

bool TunnelManager::IsHosting() const
{
    std::lock_guard lock(m_mutex);
    return m_hosting != nullptr;
}

size_t TunnelManager::HostedPeerCount() const
{
    std::lock_guard lock(m_mutex);
    return m_hosting ? m_hosting->Tunnel->PeerCount() : 0;
}

bool TunnelManager::StartJoining(PeerId aHost, JoinReady aOnReady, JoinFailed aOnFailed, std::chrono::milliseconds aTimeout)
{
    if (!m_env.Sockets || !m_env.Connect)
        return false;

    auto joining = std::make_unique<Joining>();
    joining->Link = std::make_unique<GnsLink>(m_env.Sockets);
    joining->Tunnel = std::make_unique<TunnelClient>(*joining->Link, aHost);
    if (!joining->Tunnel->Start() || !m_env.Connect(*joining->Link, aHost))
        return false;

    joining->OnReady = std::move(aOnReady);
    joining->OnFailed = std::move(aOnFailed);
    joining->Deadline = std::chrono::steady_clock::now() + aTimeout;

    {
        std::lock_guard lock(m_mutex);
        m_joining = std::move(joining);
    }
    EnsureThread();
    return true;
}

void TunnelManager::StopJoining()
{
    std::lock_guard lock(m_mutex);
    m_joining.reset();
}

void TunnelManager::Run()
{
#ifdef _WIN32
    // The default timer tick is ~15 ms, which would add that much latency to every hop.
    timeBeginPeriod(1);
#endif

    while (!m_stop)
    {
        // Callbacks are collected under the lock and run outside it, so a callback may call back
        // into the manager (for example StopJoining) without deadlocking.
        std::function<void()> callback;
        {
            std::lock_guard lock(m_mutex);

            if (m_hosting)
                m_hosting->Tunnel->Pump();

            if (m_joining)
            {
                Joining& joining = *m_joining;
                joining.Tunnel->Pump();

                if (!joining.Reported)
                {
                    if (joining.Tunnel->IsLinkUp())
                    {
                        joining.Reported = true;
                        callback = [onReady = joining.OnReady, port = joining.Tunnel->LocalPort()] { onReady(port); };
                    }
                    else if (joining.Tunnel->IsClosed())
                    {
                        joining.Reported = true;
                        callback = [onFailed = joining.OnFailed] { onFailed("the host refused or closed the connection"); };
                    }
                    else if (std::chrono::steady_clock::now() > joining.Deadline)
                    {
                        joining.Reported = true;
                        callback = [onFailed = joining.OnFailed] { onFailed("timed out waiting for the host"); };
                    }
                }
            }
        }

        if (callback)
            callback();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}
} // namespace TrueMP::Steam
