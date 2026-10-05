// Client and Server over real loopback connections through GameNetworkingSockets.

#include <catch2/catch.hpp>

#include "gns_runtime.h"

#include <Client.h>
#include <Server.h>
#include <UdpSocket.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace TrueMP::Net;

namespace
{
using Bytes = std::vector<uint8_t>;

uint16_t FreePort()
{
    TrueMP::Steam::UdpSocket probe;
    REQUIRE(probe.BindLoopback(0));
    return probe.LocalPort();
}

Bytes Noise(size_t aSize, uint32_t aSeed)
{
    Bytes bytes(aSize);
    uint32_t state = aSeed * 2654435761u + 1;
    for (auto& b : bytes)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        b = static_cast<uint8_t>(state);
    }
    return bytes;
}

// A shared log so tests can check the order things happened in across client and server.
using Log = std::vector<std::string>;

struct TestServer final : Server
{
    void OnUpdate() override { ++Ticks; }

    void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnection) override
    {
        const auto* pBytes = static_cast<const uint8_t*>(apData);
        Received.emplace_back(aConnection, Bytes(pBytes, pBytes + aSize));
        if (Handler)
            Handler(aConnection, Received.back().second);
    }

    void OnConnection(ConnectionId_t aConnection) override
    {
        Connected.push_back(aConnection);
        if (pLog)
            pLog->push_back("server:connected");
    }

    void OnDisconnection(ConnectionId_t aConnection, EDisconnectReason aReason) override
    {
        Disconnected.emplace_back(aConnection, aReason);
        if (pLog)
            pLog->push_back("server:disconnected");
    }

    int Ticks = 0;
    std::vector<ConnectionId_t> Connected;
    std::vector<std::pair<ConnectionId_t, EDisconnectReason>> Disconnected;
    std::vector<std::pair<ConnectionId_t, Bytes>> Received;
    std::function<void(ConnectionId_t, const Bytes&)> Handler;
    Log* pLog = nullptr;
};

struct TestClient final : Client
{
    void OnConsume(const void* apData, uint32_t aSize) override
    {
        const auto* pBytes = static_cast<const uint8_t*>(apData);
        Received.emplace_back(pBytes, pBytes + aSize);
        if (pLog)
            pLog->push_back("client:received");
    }

    void OnConnected() override
    {
        ++ConnectedCount;
        if (pLog)
            pLog->push_back("client:connected");
    }

    void OnDisconnected(EDisconnectReason aReason) override
    {
        Disconnects.push_back(aReason);
        if (pLog)
            pLog->push_back("client:disconnected");
    }

    void OnUpdate() override {}

    int ConnectedCount = 0;
    std::vector<EDisconnectReason> Disconnects;
    std::vector<Bytes> Received;
    Log* pLog = nullptr;
};

struct Harness
{
    TestServer Server;
    std::vector<TestClient*> Clients;
    uint16_t Port = 0;

    Harness()
    {
        TestSockets();
        Port = FreePort();
        REQUIRE(Server.Host(Port, 60));
    }

    void Pump()
    {
        Server.Update();
        for (auto* pClient : Clients)
            pClient->Update();
    }

    bool PumpUntil(const std::function<bool()>& aDone, std::chrono::milliseconds aTimeout = std::chrono::milliseconds(8000))
    {
        const auto deadline = std::chrono::steady_clock::now() + aTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            Pump();
            if (aDone())
                return true;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        return false;
    }

    // Connects a client and waits until it is fully connected (clock synchronised).
    void ConnectClient(TestClient& aClient)
    {
        Clients.push_back(&aClient);
        REQUIRE(aClient.Connect("127.0.0.1:" + std::to_string(Port)));
        REQUIRE(PumpUntil([&] { return aClient.IsConnected(); }));
    }

    std::string LastErrorlessCheck;
};
} // namespace

TEST_CASE("A client connects, synchronises its clock, and exchanges messages", "[net.link]")
{
    Harness h;
    TestClient client;
    h.ConnectClient(client);

    REQUIRE(client.ConnectedCount == 1);
    REQUIRE(client.GetClock().IsSynchronized());
    REQUIRE(h.PumpUntil([&] { return h.Server.Connected.size() == 1; }));
    REQUIRE(h.Server.GetClientCount() == 1);
    const ConnectionId_t connection = h.Server.Connected[0];

    SECTION("the client's clock agrees with the server's")
    {
        // On loopback the round trip is tiny, so the two should be within a few ms.
        const int64_t serverTick = static_cast<int64_t>(h.Server.GetTick());
        const int64_t clientTick = static_cast<int64_t>(client.GetClock().GetCurrentTick());
        REQUIRE(std::abs(serverTick - clientTick) <= 25);
    }

    SECTION("messages in both directions, small and large, reliable and not")
    {
        const std::vector<Bytes> payloads = {{1}, Noise(1000, 1), Bytes(300'000, 'z'), Noise(200'000, 2)};

        for (const auto& payload : payloads)
        {
            REQUIRE(client.Send(payload.data(), payload.size()));
            REQUIRE(h.Server.Send(connection, payload.data(), payload.size()));
        }

        REQUIRE(h.PumpUntil([&] { return h.Server.Received.size() == payloads.size() && client.Received.size() == payloads.size(); }));
        for (size_t i = 0; i < payloads.size(); ++i)
        {
            REQUIRE(h.Server.Received[i].second == payloads[i]); // reliable: arrives in order
            REQUIRE(client.Received[i] == payloads[i]);
        }
    }

    SECTION("unreliable messages are delivered on a clean link")
    {
        const Bytes payload = Noise(200, 9);
        REQUIRE(client.Send(payload.data(), payload.size(), kUnreliable));
        REQUIRE(h.PumpUntil([&] { return !h.Server.Received.empty(); }));
        REQUIRE(h.Server.Received[0].second == payload);
    }

    SECTION("an oversized payload is refused")
    {
        const Bytes tooBig(kMaxPayload + 1, 0);
        REQUIRE_FALSE(client.Send(tooBig.data(), tooBig.size()));
    }

    SECTION("the server reports where the client connected from")
    {
        REQUIRE(h.Server.GetRemoteAddress(connection).find("127.0.0.1") != std::string::npos);
        REQUIRE(h.Server.GetRemoteAddress(connection + 12345).empty());
    }
}

TEST_CASE("Compression shows up in the statistics", "[net.link]")
{
    Harness h;
    TestClient client;
    h.ConnectClient(client);

    const Bytes payload(200'000, 'a');
    REQUIRE(client.Send(payload.data(), payload.size()));

    // Statistics cover the last full second; pump past the end of the window.
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1300);
    while (std::chrono::steady_clock::now() < until)
    {
        h.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto stats = client.GetStatistics();
    REQUIRE(stats.UncompressedSentBytes >= payload.size());
    REQUIRE(stats.SentBytes < stats.UncompressedSentBytes / 10);
}

TEST_CASE("The server ticks at its configured rate", "[net.link]")
{
    Harness h;
    const auto start = std::chrono::steady_clock::now();
    h.Server.Ticks = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1000))
        h.Server.Update();

    REQUIRE(h.Server.GetTickRate() == 60);
    REQUIRE(h.Server.Ticks >= 50);
    REQUIRE(h.Server.Ticks <= 66);
}

TEST_CASE("Kicking a client", "[net.link]")
{
    Harness h;
    Log log;
    TestClient client;
    client.pLog = &log;
    h.ConnectClient(client);
    REQUIRE(h.PumpUntil([&] { return h.Server.Connected.size() == 1; }));
    const ConnectionId_t connection = h.Server.Connected[0];

    SECTION("what was queued reaches the client before the kick, and the disconnect follows")
    {
        const Bytes goodbye = {'b', 'y', 'e'};
        REQUIRE(h.Server.Send(connection, goodbye.data(), goodbye.size()));
        h.Server.Kick(connection);

        // The server's own notification is deferred, not delivered from inside Kick().
        REQUIRE(h.Server.Disconnected.empty());
        REQUIRE_FALSE(h.Server.IsAlive(connection));
        REQUIRE(h.Server.GetClientCount() == 0);

        REQUIRE(h.PumpUntil([&] { return !client.Disconnects.empty() && !h.Server.Disconnected.empty(); }));
        REQUIRE(client.Received.size() == 1);
        REQUIRE(client.Received[0] == goodbye);
        REQUIRE(client.Disconnects[0] == Client::kKicked);
        REQUIRE(h.Server.Disconnected[0].first == connection);
        REQUIRE(h.Server.Disconnected[0].second == Server::Kicked);

        // Received before disconnected, on the client.
        const auto received = std::find(log.begin(), log.end(), "client:received");
        const auto disconnected = std::find(log.begin(), log.end(), "client:disconnected");
        REQUIRE(received < disconnected);
    }

    SECTION("a handler may kick the player whose message it is handling")
    {
        h.Server.Handler = [&](ConnectionId_t aConnection, const Bytes&)
        {
            h.Server.Kick(aConnection);
            // Still safe to ask about it afterwards.
            REQUIRE_FALSE(h.Server.IsAlive(aConnection));
        };

        const Bytes message = {1, 2, 3};
        REQUIRE(client.Send(message.data(), message.size()));
        REQUIRE(h.PumpUntil([&] { return !client.Disconnects.empty() && !h.Server.Disconnected.empty(); }));
        REQUIRE(h.Server.Disconnected.size() == 1);
        REQUIRE(h.Server.Disconnected[0].second == Server::Kicked);
    }

    SECTION("kicking an unknown connection does nothing")
    {
        h.Server.Kick(connection + 999);
        h.Pump();
        REQUIRE(h.Server.GetClientCount() == 1);
        REQUIRE(h.Server.Disconnected.empty());
    }
}

TEST_CASE("Closing a connection from either side", "[net.link]")
{
    Harness h;
    TestClient client;
    h.ConnectClient(client);
    REQUIRE(h.PumpUntil([&] { return h.Server.Connected.size() == 1; }));

    SECTION("the client closes: it is told it aborted, the server sees a quit")
    {
        client.Close();
        REQUIRE(client.Disconnects == std::vector<Client::EDisconnectReason>{Client::kAborted});
        REQUIRE_FALSE(client.IsConnected());

        REQUIRE(h.PumpUntil([&] { return !h.Server.Disconnected.empty(); }));
        REQUIRE(h.Server.Disconnected[0].second == Server::Quit);
        REQUIRE(h.Server.GetClientCount() == 0);

        // Closing again is harmless and reports nothing.
        client.Close();
        REQUIRE(client.Disconnects.size() == 1);
    }

    SECTION("the server shuts down: the client finds out")
    {
        h.Server.Close();
        REQUIRE_FALSE(h.Server.IsListening());
        REQUIRE(h.PumpUntil([&] { return !client.Disconnects.empty(); }));
        REQUIRE(client.Disconnects[0] == Client::kKicked);
        REQUIRE_FALSE(client.IsConnected());
    }

    SECTION("a client can connect again after closing")
    {
        client.Close();
        REQUIRE(client.Connect("127.0.0.1:" + std::to_string(h.Port)));
        REQUIRE(h.PumpUntil([&] { return client.IsConnected(); }));
        REQUIRE(client.ConnectedCount == 2);
    }
}

TEST_CASE("Connection failures are reported", "[net.link]")
{
    TestSockets();

    SECTION("nothing is listening")
    {
        TestClient client;
        client.SetConnectTimeout(500);
        REQUIRE(client.Connect("127.0.0.1:" + std::to_string(FreePort())));

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (client.Disconnects.empty() && std::chrono::steady_clock::now() < deadline)
        {
            client.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(client.Disconnects == std::vector<Client::EDisconnectReason>{Client::kTimeout});
        REQUIRE(client.ConnectedCount == 0);
    }

    SECTION("the host name does not exist")
    {
        TestClient client;
        REQUIRE(client.Connect("no-such-host.invalid:10578"));

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (client.Disconnects.empty() && std::chrono::steady_clock::now() < deadline)
        {
            client.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(client.Disconnects == std::vector<Client::EDisconnectReason>{Client::kCannotResolve});
    }

    SECTION("a malformed endpoint is refused up front")
    {
        TestClient client;
        REQUIRE_FALSE(client.Connect(""));
        REQUIRE_FALSE(client.Connect("host:99999"));
        client.Update();
        REQUIRE(client.Disconnects.empty());
    }

    SECTION("abandoning a connection attempt reports an abort and no later failure")
    {
        TestClient client;
        client.SetConnectTimeout(300);
        REQUIRE(client.Connect("127.0.0.1:" + std::to_string(FreePort())));
        client.Update();
        client.Close();
        REQUIRE(client.Disconnects == std::vector<Client::EDisconnectReason>{Client::kAborted});

        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
        while (std::chrono::steady_clock::now() < until)
        {
            client.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(client.Disconnects.size() == 1);
    }
}

TEST_CASE("Several clients at once", "[net.link]")
{
    Harness h;
    std::vector<std::unique_ptr<TestClient>> clients;
    for (int i = 0; i < 8; ++i)
        clients.push_back(std::make_unique<TestClient>());
    for (auto& client : clients)
    {
        h.Clients.push_back(client.get());
        REQUIRE(client->Connect("127.0.0.1:" + std::to_string(h.Port)));
    }

    REQUIRE(h.PumpUntil([&] { return h.Server.GetClientCount() == 8 && std::all_of(clients.begin(), clients.end(), [](const auto& c) { return c->IsConnected(); }); }));

    const Bytes broadcast = Noise(500, 4);
    REQUIRE(h.Server.SendToAll(broadcast.data(), broadcast.size()));
    REQUIRE(h.PumpUntil([&] { return std::all_of(clients.begin(), clients.end(), [](const auto& c) { return c->Received.size() == 1; }); }));
    for (const auto& client : clients)
        REQUIRE(client->Received[0] == broadcast);

    // A message from one client is attributed to its own connection.
    const Bytes from = {42};
    REQUIRE(clients[3]->Send(from.data(), from.size()));
    REQUIRE(h.PumpUntil([&] { return !h.Server.Received.empty(); }));
    REQUIRE(h.Server.Received.size() == 1);
    REQUIRE(h.Server.Received[0].second == from);
}

TEST_CASE("A raw peer sending garbage cannot disturb the server", "[net.link]")
{
    Harness h;
    ISteamNetworkingSockets* pSockets = TestSockets();

    // Not our Client: a bare library connection that speaks nonsense.
    SteamNetworkingIPAddr address;
    address.Clear();
    address.SetIPv4(0x7f000001, h.Port);
    const HSteamNetConnection raw = pSockets->ConnectByIPAddress(address, 0, nullptr);
    REQUIRE(raw != k_HSteamNetConnection_Invalid);

    const auto connected = [&]
    {
        SteamNetConnectionInfo_t info;
        return pSockets->GetConnectionInfo(raw, &info) && info.m_eState == k_ESteamNetworkingConnectionState_Connected;
    };
    REQUIRE(h.PumpUntil(connected));
    REQUIRE(h.PumpUntil([&] { return h.Server.Connected.size() == 1; }));

    const auto send = [&](const Bytes& aBytes) { pSockets->SendMessageToConnection(raw, aBytes.data(), static_cast<uint32>(aBytes.size()), k_nSteamNetworkingSend_Reliable, nullptr); };
    send({9, 9, 9});                 // unknown frame type
    send({2});                       // a clock request with no body
    send({3, 1, 2, 3});              // a clock reply the server never asked for
    send({1, 0xff, 0xff, 0xff, 0x7f}); // "compressed" data that is corrupt
    send({0, 7, 7, 7});              // the one valid frame

    REQUIRE(h.PumpUntil([&] { return !h.Server.Received.empty(); }));
    h.Pump();
    h.Pump();

    REQUIRE(h.Server.Received.size() == 1);
    REQUIRE(h.Server.Received[0].second == (Bytes{7, 7, 7}));

    // The server carries on serving real clients.
    TestClient client;
    h.ConnectClient(client);
    REQUIRE(client.IsConnected());

    pSockets->CloseConnection(raw, 0, nullptr, false);
}
