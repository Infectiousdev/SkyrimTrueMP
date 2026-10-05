#include <catch2/catch.hpp>

#include "gns_runtime.h"

#include <TunnelManager.h>
#include <UdpSocket.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

using namespace TrueMP::Steam;

namespace
{
using Bytes = std::vector<uint8_t>;

bool WaitFor(const std::function<bool()>& aDone, std::chrono::milliseconds aTimeout = std::chrono::milliseconds(8000))
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (aDone())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

uint16_t FreePort()
{
    UdpSocket probe;
    REQUIRE(probe.BindLoopback(0));
    return probe.LocalPort();
}

SteamNetworkingIPAddr Loopback(uint16_t aPort)
{
    SteamNetworkingIPAddr address;
    address.Clear();
    address.SetIPv4(0x7f000001, aPort);
    return address;
}

// Host environment that listens on a loopback UDP port instead of Steam P2P.
TunnelManager::Environment HostEnvironment(uint16_t aPort, std::function<std::optional<bool>(PeerId)> aIsFriend)
{
    TunnelManager::Environment env;
    env.Sockets = TestSockets();
    env.LocalId = 111;
    env.Admission.IsFriend = std::move(aIsFriend);
    env.Listen = [aPort](GnsLink& aLink) { return aLink.ListenIp(aPort); };
    return env;
}

TunnelManager::Environment JoinEnvironment(uint16_t aHostPort)
{
    TunnelManager::Environment env;
    env.Sockets = TestSockets();
    env.LocalId = 222;
    env.Connect = [aHostPort](GnsLink& aLink, PeerId aHost) { return aLink.ConnectIp(Loopback(aHostPort), aHost); };
    return env;
}

constexpr PeerId kHostSteamId = 76561198000000001ull;
} // namespace

TEST_CASE("AdmissionPolicy admits friends and the allow-list, and fails closed", "[tunnel.policy]")
{
    AdmissionPolicy policy;
    policy.AllowList = {500};

    SECTION("a friend is admitted")
    {
        policy.IsFriend = [](PeerId) { return std::optional<bool>(true); };
        REQUIRE(policy(1));
    }

    SECTION("a known non-friend is refused unless allow-listed")
    {
        policy.IsFriend = [](PeerId) { return std::optional<bool>(false); };
        REQUIRE_FALSE(policy(1));
        REQUIRE(policy(500));
    }

    SECTION("when Steam cannot say, only the allow-list gets in")
    {
        policy.IsFriend = [](PeerId) { return std::optional<bool>(); };
        REQUIRE_FALSE(policy(1));
        REQUIRE(policy(500));
    }

    SECTION("with no friend check at all, nobody but the allow-list gets in")
    {
        policy.IsFriend = nullptr;
        REQUIRE_FALSE(policy(1));
        REQUIRE(policy(500));
    }
}

TEST_CASE("A friend joins a hosted server through the tunnel", "[tunnel.manager]")
{
    // STServer stand-in: echoes every datagram back to its sender.
    UdpSocket server;
    REQUIRE(server.BindLoopback(0));
    std::atomic<bool> serving{true};
    std::thread echo(
        [&]
        {
            uint8_t buffer[2048];
            while (serving)
            {
                uint16_t from = 0;
                const int got = server.Receive(buffer, sizeof(buffer), &from);
                if (got > 0)
                    server.SendTo(buffer, static_cast<size_t>(got), from);
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    struct StopEcho
    {
        std::atomic<bool>& Flag;
        std::thread& Thread;
        ~StopEcho()
        {
            Flag = false;
            Thread.join();
        }
    } stopEcho{serving, echo};

    const uint16_t hostPort = FreePort();
    TunnelManager host(HostEnvironment(hostPort, [](PeerId) { return std::optional<bool>(true); }));
    REQUIRE(host.StartHosting(server.LocalPort()));
    REQUIRE(host.IsHosting());

    TunnelManager joiner(JoinEnvironment(hostPort));
    std::atomic<uint16_t> readyPort{0};
    std::atomic<bool> failed{false};
    REQUIRE(joiner.StartJoining(kHostSteamId, [&](uint16_t aPort) { readyPort = aPort; }, [&](const std::string&) { failed = true; }));

    REQUIRE(WaitFor([&] { return readyPort != 0 || failed; }));
    REQUIRE_FALSE(failed);

    // The game would now connect to 127.0.0.1:readyPort.
    UdpSocket game;
    REQUIRE(game.BindLoopback(0));
    const Bytes request = {1, 2, 3, 4, 5, 6};
    REQUIRE(game.SendTo(request.data(), request.size(), readyPort));

    uint8_t buffer[64];
    int got = 0;
    REQUIRE(WaitFor([&] { return (got = game.Receive(buffer, sizeof(buffer))) > 0; }));
    REQUIRE(Bytes(buffer, buffer + got) == request);
    REQUIRE(host.HostedPeerCount() == 1);

    SECTION("stopping hosting stops forwarding")
    {
        host.StopHosting();
        REQUIRE_FALSE(host.IsHosting());
        REQUIRE(host.HostedPeerCount() == 0);
    }
}

TEST_CASE("A peer the host does not admit is told no", "[tunnel.manager]")
{
    UdpSocket server;
    REQUIRE(server.BindLoopback(0));

    const uint16_t hostPort = FreePort();
    TunnelManager host(HostEnvironment(hostPort, [](PeerId) { return std::optional<bool>(false); }));
    REQUIRE(host.StartHosting(server.LocalPort()));

    TunnelManager joiner(JoinEnvironment(hostPort));
    std::atomic<bool> ready{false}, failed{false};
    std::string reason;
    REQUIRE(joiner.StartJoining(
        kHostSteamId, [&](uint16_t) { ready = true; },
        [&](const std::string& aReason)
        {
            reason = aReason;
            failed = true;
        }));

    REQUIRE(WaitFor([&] { return ready || failed; }));
    REQUIRE(failed);
    REQUIRE_FALSE(ready);
    REQUIRE(host.HostedPeerCount() == 0);
}

TEST_CASE("Joining a host that never answers times out", "[tunnel.manager]")
{
    // Nothing listens on this port.
    TunnelManager joiner(JoinEnvironment(FreePort()));
    std::atomic<bool> failed{false};
    std::string reason;
    REQUIRE(joiner.StartJoining(
        kHostSteamId, [](uint16_t) {},
        [&](const std::string& aReason)
        {
            reason = aReason;
            failed = true;
        },
        std::chrono::milliseconds(400)));

    REQUIRE(WaitFor([&] { return failed.load(); }));
    REQUIRE(reason.find("timed out") != std::string::npos);
}

TEST_CASE("A manager without Steam sockets does nothing", "[tunnel.manager]")
{
    TunnelManager unusable{TunnelManager::Environment{}};
    REQUIRE_FALSE(unusable.StartHosting(10578));
    REQUIRE_FALSE(unusable.StartJoining(1, [](uint16_t) {}, [](const std::string&) {}));
    REQUIRE_FALSE(unusable.IsHosting());
}
