#pragma once

// SkyrimTrueMP: lets Steam friends join each other without port forwarding.
//
//  * A player who joins a server on their own machine also offers it to their Steam friends.
//  * A friend types "steam:<host's SteamID64>" where the IP address goes; the game then connects
//    to a local port that the tunnel carries to the host.
//
// Steam is initialised lazily, the first time one of these is used, so the game starts exactly
// as before for players who never use it.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace TrueMP::Steam
{
class TunnelManager;
}

struct SteamTunnelService
{
    static SteamTunnelService& Get() noexcept;

    ~SteamTunnelService();

    // True for "steam:<digits>" addresses.
    static bool IsSteamAddress(const std::string& aAddress);

    using EndpointReady = std::function<void(const std::string& aEndpoint)>;
    using Failed = std::function<void(const std::string& aReason)>;

    // Starts joining the host named by aAddress. aOnReady("127.0.0.1:<port>") runs once the link
    // to the host is up; aOnFailed explains why it could not be. Both may run on another thread.
    void Join(const std::string& aAddress, EndpointReady aOnReady, Failed aOnFailed);
    void StopJoining();

    // Lets admitted Steam friends reach the server on this machine's aServerPort.
    void OfferToFriends(uint16_t aServerPort);

private:
    SteamTunnelService() = default;

    TrueMP::Steam::TunnelManager* Manager(std::string& aError);

    std::mutex m_mutex;
    std::unique_ptr<TrueMP::Steam::TunnelManager> m_manager;
};
