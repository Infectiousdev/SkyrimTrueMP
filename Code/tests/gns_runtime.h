#pragma once

// Initialises the standalone GameNetworkingSockets library once for all tests in this binary.

#include <steam/steamnetworkingsockets.h>

#include <catch2/catch.hpp>

inline ISteamNetworkingSockets* TestSockets()
{
    static ISteamNetworkingSockets* s_pSockets = []
    {
        SteamDatagramErrMsg error;
        REQUIRE(GameNetworkingSockets_Init(nullptr, error));
        return SteamNetworkingSockets();
    }();
    return s_pSockets;
}
