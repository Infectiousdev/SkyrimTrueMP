#pragma once

// SkyrimTrueMP: connects the tunnel to the Steam client that the game already uses.
//
// Skyrim loads steam_api64.dll and initialises Steam itself, so nothing new is initialised
// here: we look up the interfaces the Steam client already provides to this process and hand
// them to the (portable) TunnelManager. Everything that touches Windows or Steam lives in
// SteamRuntime.cpp behind _WIN32; elsewhere AcquireSteamEnvironment() simply reports failure.

#include "TunnelManager.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>

namespace TrueMP::Steam
{
// Virtual port used for the game's P2P connections.
inline constexpr int kSteamVirtualPort = 28564;

// SteamID64s that may connect even if they are not Steam friends: one per line, '#' starts a
// comment, anything that is not a number is ignored. A missing file means an empty list.
[[nodiscard]] std::set<PeerId> LoadAllowList(const std::filesystem::path& aFile);

// Finds Steam's networking and friends interfaces in this process. Returns nullopt, with a
// reason a player can act on in aError, if Steam is not available.
[[nodiscard]] std::optional<TunnelManager::Environment> AcquireSteamEnvironment(std::string& aError, std::set<PeerId> aAllowList);

// Parses "steam:<SteamID64>" (also "steam://<id>"). Returns 0 if aAddress is not a Steam address.
[[nodiscard]] PeerId ParseSteamAddress(const std::string& aAddress);
} // namespace TrueMP::Steam
