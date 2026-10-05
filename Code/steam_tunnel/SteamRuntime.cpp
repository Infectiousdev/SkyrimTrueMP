#include "SteamRuntime.h"

#include <cctype>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace TrueMP::Steam
{
std::set<PeerId> LoadAllowList(const std::filesystem::path& aFile)
{
    std::set<PeerId> ids;

    std::ifstream file(aFile);
    std::string line;
    while (std::getline(file, line))
    {
        const auto comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);

        // Trim whitespace, including the carriage return of a Windows file.
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
            ++start;
        line.erase(0, start);

        if (line.empty() || line.size() > 20)
            continue;

        bool digits = true;
        for (char c : line)
            digits = digits && std::isdigit(static_cast<unsigned char>(c));
        if (!digits)
            continue;

        try
        {
            const unsigned long long value = std::stoull(line);
            if (value != 0)
                ids.insert(static_cast<PeerId>(value));
        }
        catch (const std::exception&)
        {
            // Too large for 64 bits; not a SteamID.
        }
    }
    return ids;
}

PeerId ParseSteamAddress(const std::string& aAddress)
{
    static const std::string kPrefix = "steam:";
    if (aAddress.compare(0, kPrefix.size(), kPrefix) != 0)
        return 0;

    std::string id = aAddress.substr(kPrefix.size());
    if (id.compare(0, 2, "//") == 0)
        id.erase(0, 2);

    if (id.empty() || id.size() > 20)
        return 0;
    for (char c : id)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return 0;
    }

    try
    {
        return static_cast<PeerId>(std::stoull(id));
    }
    catch (const std::exception&)
    {
        return 0;
    }
}

#ifdef _WIN32

namespace
{
using GetHSteamUserFn = int(__cdecl*)();
using FindInterfaceFn = void*(__cdecl*)(int aUser, const char* aVersion);
using AccessorFn = void*(__cdecl*)();
using GetFriendRelationshipFn = int(__cdecl*)(void* apFriends, uint64_t aSteamId);

// EFriendRelationship::k_EFriendRelationshipFriend
constexpr int kRelationshipFriend = 3;

template <class Fn> Fn Resolve(HMODULE aModule, const char* aName)
{
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(aModule, aName)));
}
} // namespace

std::optional<TunnelManager::Environment> AcquireSteamEnvironment(std::string& aError, std::set<PeerId> aAllowList)
{
    // Skyrim loads this itself when it is started through Steam.
    const HMODULE steamApi = GetModuleHandleW(L"steam_api64.dll");
    if (!steamApi)
    {
        aError = "steam_api64.dll is not loaded. Start Skyrim through Steam to use Steam friends.";
        return std::nullopt;
    }

    const auto getUser = Resolve<GetHSteamUserFn>(steamApi, "SteamAPI_GetHSteamUser");
    const auto findInterface = Resolve<FindInterfaceFn>(steamApi, "SteamInternal_FindOrCreateUserInterface");
    if (!getUser || !findInterface)
    {
        aError = "This Steam API library is too old to open Steam networking.";
        return std::nullopt;
    }

    const int user = getUser();
    if (user == 0)
    {
        aError = "Steam is not initialised in this game. Is Steam running and are you logged in?";
        return std::nullopt;
    }

    // The interface version must match the layout of the headers we compile against.
    auto* pSockets = static_cast<ISteamNetworkingSockets*>(findInterface(user, STEAMNETWORKINGSOCKETS_INTERFACE_VERSION));
    if (!pSockets)
    {
        aError = "The running Steam client does not offer " STEAMNETWORKINGSOCKETS_INTERFACE_VERSION ". Update Steam.";
        return std::nullopt;
    }

    SteamNetworkingIdentity identity;
    identity.Clear();
    pSockets->GetIdentity(&identity);

    TunnelManager::Environment env;
    env.Sockets = pSockets;
    env.LocalId = identity.GetSteamID64();
    env.Admission.AllowList = std::move(aAllowList);

    // Friendship: use the flat API accessor for the ISteamFriends version this DLL was built
    // with, so the interface and the function that calls it always agree.
    void* pFriends = nullptr;
    for (int version = 25; version >= 8 && !pFriends; --version)
    {
        char name[48];
        std::snprintf(name, sizeof(name), "SteamAPI_SteamFriends_v%03d", version);
        if (const auto accessor = Resolve<AccessorFn>(steamApi, name))
            pFriends = accessor();
    }
    const auto getRelationship = Resolve<GetFriendRelationshipFn>(steamApi, "SteamAPI_ISteamFriends_GetFriendRelationship");

    if (pFriends && getRelationship)
    {
        env.Admission.IsFriend = [pFriends, getRelationship](PeerId aPeer) -> std::optional<bool> { return getRelationship(pFriends, aPeer) == kRelationshipFriend; };
    }
    // else: IsFriend stays empty and the policy admits only the allow-list (fail closed).

    env.Listen = [](GnsLink& aLink) { return aLink.ListenP2P(kSteamVirtualPort); };
    env.Connect = [](GnsLink& aLink, PeerId aHost) { return aLink.ConnectP2P(aHost, kSteamVirtualPort); };
    return env;
}

#else

std::optional<TunnelManager::Environment> AcquireSteamEnvironment(std::string& aError, std::set<PeerId>)
{
    aError = "Steam friends are only available in the Windows game.";
    return std::nullopt;
}

#endif
} // namespace TrueMP::Steam
