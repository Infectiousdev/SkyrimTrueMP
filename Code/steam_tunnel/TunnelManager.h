#pragma once

// SkyrimTrueMP: runs the Steam tunnel for a player.
//
//  * Hosting: the player runs the game server on their own machine; admitted friends reach it
//    through the tunnel.
//  * Joining: the player connects to a friend's SteamID; the game then connects to a local UDP
//    port instead of an IP address.
//
// The manager owns the links and tunnels and pumps them from one worker thread, so tunnel
// latency does not depend on the game's frame rate. All platform specifics (where the
// ISteamNetworkingSockets comes from, how friendship is checked) arrive in Environment, so
// everything here is portable and tested with loopback IP links.

#include "GnsLink.h"
#include "Tunnel.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

namespace TrueMP::Steam
{
// Who may use a hosted server. Friends are admitted; if friendship cannot be determined, only
// explicitly allow-listed SteamIDs are (fail closed). The allow-list also admits non-friends.
struct AdmissionPolicy
{
    // true/false when known, nullopt when Steam cannot say.
    std::function<std::optional<bool>(PeerId)> IsFriend;
    std::set<PeerId> AllowList;

    [[nodiscard]] bool operator()(PeerId aPeer) const;
};

class TunnelManager
{
public:
    struct Environment
    {
        ISteamNetworkingSockets* Sockets = nullptr;
        PeerId LocalId = 0; // our own SteamID64, to show the host so friends can join
        AdmissionPolicy Admission;

        // How links open connections. Steam P2P in the game; loopback IP in tests.
        std::function<bool(GnsLink&)> Listen;
        std::function<bool(GnsLink&, PeerId aHost)> Connect;
    };

    using JoinReady = std::function<void(uint16_t aLocalPort)>;
    using JoinFailed = std::function<void(const std::string& aReason)>;

    explicit TunnelManager(Environment aEnvironment);
    ~TunnelManager();

    TunnelManager(const TunnelManager&) = delete;
    TunnelManager& operator=(const TunnelManager&) = delete;

    // Host: forward admitted friends to the server on 127.0.0.1:aServerPort. Calling it again
    // retargets. Returns false if the link could not listen.
    bool StartHosting(uint16_t aServerPort);
    void StopHosting();
    [[nodiscard]] bool IsHosting() const;
    [[nodiscard]] size_t HostedPeerCount() const;

    // Join: connect to the host through the tunnel. aOnReady(port) is called from the worker
    // thread once the link is up (connect the game to 127.0.0.1:port then); aOnFailed if it
    // does not come up within aTimeout or the host closes it.
    bool StartJoining(PeerId aHost, JoinReady aOnReady, JoinFailed aOnFailed, std::chrono::milliseconds aTimeout = std::chrono::seconds(20));
    void StopJoining();

    [[nodiscard]] PeerId LocalId() const noexcept { return m_env.LocalId; }

private:
    struct Hosting
    {
        std::unique_ptr<GnsLink> Link;
        std::unique_ptr<TunnelHost> Tunnel;
    };

    struct Joining
    {
        std::unique_ptr<GnsLink> Link;
        std::unique_ptr<TunnelClient> Tunnel;
        JoinReady OnReady;
        JoinFailed OnFailed;
        std::chrono::steady_clock::time_point Deadline;
        bool Reported = false;
    };

    void EnsureThread();
    void Run();

    Environment m_env;
    mutable std::mutex m_mutex;
    std::unique_ptr<Hosting> m_hosting;
    std::unique_ptr<Joining> m_joining;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};
} // namespace TrueMP::Steam
