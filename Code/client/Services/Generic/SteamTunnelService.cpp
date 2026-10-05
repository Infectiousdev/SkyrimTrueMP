#include <TiltedOnlinePCH.h>

#include <Services/SteamTunnelService.h>

#include <SteamRuntime.h>
#include <TunnelManager.h>

#include <filesystem>

using namespace TrueMP::Steam;

SteamTunnelService& SteamTunnelService::Get() noexcept
{
    static SteamTunnelService s_instance;
    return s_instance;
}

SteamTunnelService::~SteamTunnelService() = default;

bool SteamTunnelService::IsSteamAddress(const std::string& aAddress)
{
    return aAddress.compare(0, 6, "steam:") == 0;
}

TunnelManager* SteamTunnelService::Manager(std::string& aError)
{
    std::lock_guard lock(m_mutex);

    if (!m_manager)
    {
        // Not cached on failure: Steam may be started, or the player may fix their setup, later.
        const auto allowList = std::filesystem::current_path() / "Data" / "SkyrimTogetherReborn" / "config" / "steam_allowlist.txt";
        if (auto environment = AcquireSteamEnvironment(aError, LoadAllowList(allowList)))
            m_manager = std::make_unique<TunnelManager>(std::move(*environment));
    }

    return m_manager.get();
}

void SteamTunnelService::Join(const std::string& aAddress, EndpointReady aOnReady, Failed aOnFailed)
{
    const PeerId host = ParseSteamAddress(aAddress);
    if (host == 0)
    {
        aOnFailed("that is not a valid SteamID. Use steam:<the host's 17 digit SteamID64>");
        return;
    }

    std::string error;
    TunnelManager* pManager = Manager(error);
    if (!pManager)
    {
        spdlog::error("Cannot join {} through Steam: {}", aAddress, error);
        aOnFailed(error);
        return;
    }

    pManager->StopJoining();
    const bool started = pManager->StartJoining(
        host, [aOnReady](uint16_t aPort) { aOnReady("127.0.0.1:" + std::to_string(aPort)); },
        [aOnFailed](const std::string& aReason)
        {
            spdlog::error("Steam connection failed: {}", aReason);
            aOnFailed(aReason);
        });

    if (!started)
        aOnFailed("could not open a Steam connection to that player");
}

void SteamTunnelService::StopJoining()
{
    // Do not initialise Steam just to stop something that was never started.
    std::lock_guard lock(m_mutex);
    if (m_manager)
        m_manager->StopJoining();
}

void SteamTunnelService::OfferToFriends(uint16_t aServerPort)
{
    std::string error;
    TunnelManager* pManager = Manager(error);
    if (!pManager)
    {
        // Steam is optional when hosting, so this is only informational.
        spdlog::info("Steam friends cannot join this server: {}", error);
        return;
    }

    if (!pManager->StartHosting(aServerPort))
    {
        spdlog::warn("Could not open the server to Steam friends");
        return;
    }

    spdlog::info("Steam friends can join this server with: steam:{}", pManager->LocalId());
}
