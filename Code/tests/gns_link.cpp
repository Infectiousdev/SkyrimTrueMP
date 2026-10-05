// The Steam link is written against ISteamNetworkingSockets, so here it runs over the real
// standalone GameNetworkingSockets on loopback IP. Only the two calls that open a Steam P2P
// connection (ListenP2P/ConnectP2P) are not exercised.

#include <catch2/catch.hpp>

#include "gns_runtime.h"

#include <GnsLink.h>
#include <Tunnel.h>
#include <UdpSocket.h>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace TrueMP::Steam;

namespace
{
using Bytes = std::vector<uint8_t>;

struct Recorder final : ILinkListener
{
    void OnPeerConnected(PeerId aPeer) override { Connected.push_back(aPeer); }
    void OnPeerDisconnected(PeerId aPeer) override { Disconnected.push_back(aPeer); }
    void OnDatagram(PeerId aPeer, const uint8_t* apData, size_t aSize) override { Datagrams.emplace_back(aPeer, Bytes(apData, apData + aSize)); }

    std::vector<PeerId> Connected;
    std::vector<PeerId> Disconnected;
    std::vector<std::pair<PeerId, Bytes>> Datagrams;
};

bool PumpUntil(const std::function<void()>& aPump, const std::function<bool()>& aDone, std::chrono::milliseconds aTimeout = std::chrono::milliseconds(8000))
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        aPump();
        if (aDone())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

SteamNetworkingIPAddr Loopback(uint16_t aPort)
{
    SteamNetworkingIPAddr address;
    address.Clear();
    address.SetIPv4(0x7f000001, aPort);
    return address;
}

constexpr PeerId kHostLabel = 77;

// The library cannot pick a listen port itself, so find a free one first.
uint16_t FreePort()
{
    UdpSocket probe;
    REQUIRE(probe.BindLoopback(0));
    return probe.LocalPort();
}
} // namespace

TEST_CASE("GnsLink connects a client to a host and carries datagrams both ways", "[gnslink]")
{
    GnsLink host(TestSockets(), [](PeerId) { return true; });
    REQUIRE(host.ListenIp(FreePort()));
    REQUIRE(host.ListenPort() != 0);

    GnsLink client(TestSockets());
    REQUIRE(client.ConnectIp(Loopback(host.ListenPort()), kHostLabel));

    Recorder hostEvents, clientEvents;
    const auto pump = [&]
    {
        host.Poll(hostEvents);
        client.Poll(clientEvents);
    };

    REQUIRE(PumpUntil(pump, [&] { return hostEvents.Connected.size() == 1 && clientEvents.Connected.size() == 1; }));
    REQUIRE(clientEvents.Connected[0] == kHostLabel);
    const PeerId clientAsSeenByHost = hostEvents.Connected[0];

    const Bytes toHost = {1, 2, 3, 4, 5};
    const Bytes toClient = {9, 8, 7};
    REQUIRE(client.Send(kHostLabel, toHost.data(), toHost.size()));
    REQUIRE(host.Send(clientAsSeenByHost, toClient.data(), toClient.size()));

    REQUIRE(PumpUntil(pump, [&] { return !hostEvents.Datagrams.empty() && !clientEvents.Datagrams.empty(); }));
    REQUIRE(hostEvents.Datagrams[0].first == clientAsSeenByHost);
    REQUIRE(hostEvents.Datagrams[0].second == toHost);
    REQUIRE(clientEvents.Datagrams[0].first == kHostLabel);
    REQUIRE(clientEvents.Datagrams[0].second == toClient);
}

TEST_CASE("GnsLink refuses peers the admit function rejects", "[gnslink]")
{
    GnsLink host(TestSockets(), [](PeerId) { return false; });
    REQUIRE(host.ListenIp(FreePort()));

    GnsLink client(TestSockets());
    REQUIRE(client.ConnectIp(Loopback(host.ListenPort()), kHostLabel));

    Recorder hostEvents, clientEvents;
    REQUIRE(PumpUntil(
        [&]
        {
            host.Poll(hostEvents);
            client.Poll(clientEvents);
        },
        [&] { return !clientEvents.Disconnected.empty(); }));

    // The client learns it was turned away; the host never treated it as a player.
    REQUIRE(clientEvents.Disconnected[0] == kHostLabel);
    REQUIRE(clientEvents.Connected.empty());
    REQUIRE(hostEvents.Connected.empty());
    REQUIRE(host.RejectedCount() == 1);
}

TEST_CASE("A host link without an admission policy refuses to listen", "[gnslink]")
{
    GnsLink noPolicy(TestSockets());
    REQUIRE_FALSE(noPolicy.ListenIp(FreePort()));
    REQUIRE_FALSE(noPolicy.ListenP2P(0));
}

TEST_CASE("GnsLink reports a departing peer", "[gnslink]")
{
    GnsLink host(TestSockets(), [](PeerId) { return true; });
    REQUIRE(host.ListenIp(FreePort()));

    Recorder hostEvents;
    {
        GnsLink client(TestSockets());
        REQUIRE(client.ConnectIp(Loopback(host.ListenPort()), kHostLabel));
        Recorder clientEvents;
        REQUIRE(PumpUntil(
            [&]
            {
                host.Poll(hostEvents);
                client.Poll(clientEvents);
            },
            [&] { return hostEvents.Connected.size() == 1; }));
    } // client destroyed: its connection is closed

    REQUIRE(PumpUntil([&] { host.Poll(hostEvents); }, [&] { return hostEvents.Disconnected.size() == 1; }));
    REQUIRE(hostEvents.Disconnected[0] == hostEvents.Connected[0]);
}

TEST_CASE("Two links in one process do not see each other's connections", "[gnslink]")
{
    // RunCallbacks() services every connection on the interface, so each connection must
    // reach only the link that owns it.
    GnsLink hostA(TestSockets(), [](PeerId) { return true; });
    GnsLink hostB(TestSockets(), [](PeerId) { return true; });
    REQUIRE(hostA.ListenIp(FreePort()));
    REQUIRE(hostB.ListenIp(FreePort()));

    GnsLink clientA(TestSockets()), clientB(TestSockets());
    REQUIRE(clientA.ConnectIp(Loopback(hostA.ListenPort()), kHostLabel));
    REQUIRE(clientB.ConnectIp(Loopback(hostB.ListenPort()), kHostLabel));

    Recorder eventsHostA, eventsHostB, eventsClientA, eventsClientB;

    // Pump only A's side: B's connections are serviced too, but must not leak into A.
    REQUIRE(PumpUntil(
        [&]
        {
            hostA.Poll(eventsHostA);
            clientA.Poll(eventsClientA);
        },
        [&] { return eventsHostA.Connected.size() == 1 && eventsClientA.Connected.size() == 1; }));
    REQUIRE(eventsHostA.Connected.size() == 1);

    // B's events were queued on B and arrive when B is finally polled.
    REQUIRE(PumpUntil(
        [&]
        {
            hostB.Poll(eventsHostB);
            clientB.Poll(eventsClientB);
        },
        [&] { return eventsHostB.Connected.size() == 1 && eventsClientB.Connected.size() == 1; }));
    REQUIRE(eventsHostA.Connected.size() == 1);
}

TEST_CASE("The tunnel works over a real GameNetworkingSockets link", "[gnslink][tunnel]")
{
    // STServer stand-in and game stand-in, plain loopback UDP.
    UdpSocket server;
    REQUIRE(server.BindLoopback(0));
    UdpSocket game;
    REQUIRE(game.BindLoopback(0));

    GnsLink hostLink(TestSockets(), [](PeerId) { return true; });
    REQUIRE(hostLink.ListenIp(FreePort()));
    TunnelHost host(hostLink, {server.LocalPort(), 8}, [](PeerId) { return true; });

    GnsLink clientLink(TestSockets());
    REQUIRE(clientLink.ConnectIp(Loopback(hostLink.ListenPort()), kHostLabel));
    TunnelClient client(clientLink, kHostLabel);
    REQUIRE(client.Start());

    const auto pump = [&]
    {
        host.Pump();
        client.Pump();
    };
    REQUIRE(PumpUntil(pump, [&] { return client.IsLinkUp() && host.PeerCount() == 1; }));

    const Bytes request = {10, 20, 30, 40};
    REQUIRE(game.SendTo(request.data(), request.size(), client.LocalPort()));

    uint8_t buffer[256];
    uint16_t from = 0;
    int got = 0;
    REQUIRE(PumpUntil(pump, [&] { return (got = server.Receive(buffer, sizeof(buffer), &from)) > 0; }));
    REQUIRE(Bytes(buffer, buffer + got) == request);

    const Bytes reply = {99, 98, 97};
    REQUIRE(server.SendTo(reply.data(), reply.size(), from));
    REQUIRE(PumpUntil(pump, [&] { return (got = game.Receive(buffer, sizeof(buffer))) > 0; }));
    REQUIRE(Bytes(buffer, buffer + got) == reply);
}
