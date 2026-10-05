#pragma once

// Internal: the parts of GameNetworkingSockets shared by Client and Server. Not for consumers
// of the library; including this pulls in the Valve headers.

#include <steam/steamnetworkingsockets.h>

namespace TrueMP::Net::Gns
{
// Receives connection state changes for the connections it owns.
struct StatusSink
{
    virtual ~StatusSink() = default;
    virtual void OnStatusChanged(const SteamNetConnectionStatusChangedCallback_t& aChange) = 0;
};

// Reference counted: the library is initialised for the first user and shut down after the last.
[[nodiscard]] bool Acquire();
void Release();

[[nodiscard]] ISteamNetworkingSockets* Sockets();

// RunCallbacks() services every connection on the interface, not just ours, so each connection
// carries its owner as user data and the owner must still be registered when a callback lands.
void Register(StatusSink* apSink);
void Unregister(StatusSink* apSink);

// Fills aOptions (room for 4) with the options every connection and listen socket gets and
// returns how many were set: the status callback, the owner, a send rate ceiling suited to a
// busy game server, and optionally a connect timeout (milliseconds, 0 for the default).
int MakeOptions(StatusSink* apOwner, SteamNetworkingConfigValue_t* apOptions, int aConnectTimeoutMs);
} // namespace TrueMP::Net::Gns
