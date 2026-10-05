#pragma once

// Gives the tests one shared handle on the standalone GameNetworkingSockets library. It takes a
// reference through the network library's own counter and never releases it, so the library is not
// shut down underneath tests that still use it.

#include <Gns.h>

#include <catch2/catch.hpp>

inline ISteamNetworkingSockets* TestSockets()
{
    static ISteamNetworkingSockets* s_pSockets = []
    {
        REQUIRE(TrueMP::Net::Gns::Acquire());
        return TrueMP::Net::Gns::Sockets();
    }();
    return s_pSockets;
}
