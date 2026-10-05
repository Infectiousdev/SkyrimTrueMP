#include <catch2/catch.hpp>

#include <LoopbackLink.h>
#include <Tunnel.h>
#include <UdpSocket.h>

#include <chrono>
#include <functional>
#include <thread>
#include <vector>

using namespace TrueMP::Steam;

namespace
{
using Bytes = std::vector<uint8_t>;

Bytes Payload(size_t aSize, uint8_t aSeed)
{
    Bytes bytes(aSize);
    for (size_t i = 0; i < aSize; ++i)
        bytes[i] = static_cast<uint8_t>(aSeed + i * 7);
    return bytes;
}

// Calls aPump repeatedly until aDone is true; fails the test after aTimeout.
bool PumpUntil(const std::function<void()>& aPump, const std::function<bool()>& aDone, std::chrono::milliseconds aTimeout = std::chrono::milliseconds(3000))
{
    const auto deadline = std::chrono::steady_clock::now() + aTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        aPump();
        if (aDone())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Stands in for STServer: a loopback UDP socket that records what arrives and who from.
struct FakeServer
{
    struct Received
    {
        Bytes Data;
        uint16_t FromPort;
    };

    FakeServer() { REQUIRE(Socket.BindLoopback(0)); }

    void Poll()
    {
        uint8_t buffer[4096];
        uint16_t from = 0;
        int got;
        while ((got = Socket.Receive(buffer, sizeof(buffer), &from)) > 0)
            Inbox.push_back({Bytes(buffer, buffer + got), from});
    }

    void Reply(uint16_t aPort, const Bytes& aData) { Socket.SendTo(aData.data(), aData.size(), aPort); }

    UdpSocket Socket;
    std::vector<Received> Inbox;
};

// Stands in for the game: sends from one local port, receives replies on it.
struct FakeGame
{
    FakeGame() { REQUIRE(Socket.BindLoopback(0)); }

    void Poll()
    {
        uint8_t buffer[4096];
        int got;
        while ((got = Socket.Receive(buffer, sizeof(buffer))) > 0)
            Inbox.emplace_back(buffer, buffer + got);
    }

    UdpSocket Socket;
    std::vector<Bytes> Inbox;
};

TunnelHost::AdmitFn AdmitAll()
{
    return [](PeerId) { return true; };
}
} // namespace

TEST_CASE("UdpSocket loopback", "[tunnel.udp]")
{
    UdpSocket a, b;
    REQUIRE(a.BindLoopback(0));
    REQUIRE(b.BindLoopback(0));
    REQUIRE(a.LocalPort() != 0);
    REQUIRE(a.LocalPort() != b.LocalPort());

    uint8_t buffer[16];
    REQUIRE(a.Receive(buffer, sizeof(buffer)) == 0); // nothing waiting, and it must not block

    const Bytes message = {1, 2, 3, 4};
    REQUIRE(a.SendTo(message.data(), message.size(), b.LocalPort()));

    uint16_t from = 0;
    int got = 0;
    REQUIRE(PumpUntil([] {}, [&] { return (got = b.Receive(buffer, sizeof(buffer), &from)) != 0; }));
    REQUIRE(got == 4);
    REQUIRE(from == a.LocalPort());
}

TEST_CASE("Tunnel carries datagrams to the server and replies back", "[tunnel.echo]")
{
    FakeServer server;
    LoopbackNetwork network;
    TunnelHost host(network.Host(), {server.Socket.LocalPort(), 8}, AdmitAll());

    auto clientLink = network.Connect(100);
    TunnelClient client(*clientLink, LoopbackNetwork::kHostId);
    REQUIRE(client.Start());

    FakeGame game;
    const auto pump = [&]
    {
        host.Pump();
        client.Pump();
        server.Poll();
        game.Poll();
    };

    SECTION("datagrams of many sizes arrive intact, and so do the replies")
    {
        for (size_t size : {size_t(1), size_t(100), size_t(1200), size_t(2048)})
        {
            server.Inbox.clear();
            game.Inbox.clear();

            const Bytes request = Payload(size, 3);
            game.Socket.SendTo(request.data(), request.size(), client.LocalPort());

            INFO("size " << size);
            REQUIRE(PumpUntil(pump, [&] { return !server.Inbox.empty(); }));
            REQUIRE(server.Inbox[0].Data == request);

            const Bytes reply = Payload(size, 99);
            server.Reply(server.Inbox[0].FromPort, reply);
            REQUIRE(PumpUntil(pump, [&] { return !game.Inbox.empty(); }));
            REQUIRE(game.Inbox[0] == reply);
        }
    }

    SECTION("datagrams keep their order")
    {
        for (uint8_t i = 0; i < 100; ++i)
        {
            const Bytes datagram = {i, uint8_t(i + 1)};
            game.Socket.SendTo(datagram.data(), datagram.size(), client.LocalPort());
            pump();
        }

        REQUIRE(PumpUntil(pump, [&] { return server.Inbox.size() == 100; }));
        for (uint8_t i = 0; i < 100; ++i)
            REQUIRE(server.Inbox[i].Data == (Bytes{i, uint8_t(i + 1)}));
    }

    SECTION("an oversized datagram is dropped, not forwarded or truncated")
    {
        const Bytes huge = Payload(kMaxDatagram + 100, 1);
        game.Socket.SendTo(huge.data(), huge.size(), client.LocalPort());
        REQUIRE(PumpUntil(pump, [&] { return client.Stats().DatagramsDropped == 1; }));
        REQUIRE(server.Inbox.empty());
    }
}

TEST_CASE("Each remote player is a distinct client to the server", "[tunnel.multi]")
{
    FakeServer server;
    LoopbackNetwork network;
    TunnelHost host(network.Host(), {server.Socket.LocalPort(), 8}, AdmitAll());

    auto linkA = network.Connect(100);
    auto linkB = network.Connect(200);
    TunnelClient clientA(*linkA, LoopbackNetwork::kHostId);
    TunnelClient clientB(*linkB, LoopbackNetwork::kHostId);
    REQUIRE(clientA.Start());
    REQUIRE(clientB.Start());

    FakeGame gameA, gameB;
    const auto pump = [&]
    {
        host.Pump();
        clientA.Pump();
        clientB.Pump();
        server.Poll();
        gameA.Poll();
        gameB.Poll();
    };

    gameA.Socket.SendTo(Payload(10, 1).data(), 10, clientA.LocalPort());
    gameB.Socket.SendTo(Payload(10, 2).data(), 10, clientB.LocalPort());
    REQUIRE(PumpUntil(pump, [&] { return server.Inbox.size() == 2; }));
    REQUIRE(host.PeerCount() == 2);

    // The server tells the two apart by source port.
    REQUIRE(server.Inbox[0].FromPort != server.Inbox[1].FromPort);

    // A reply goes only to the player it was addressed to.
    const auto& fromA = server.Inbox[0].Data == Payload(10, 1) ? server.Inbox[0] : server.Inbox[1];
    server.Reply(fromA.FromPort, Payload(20, 55));
    REQUIRE(PumpUntil(pump, [&] { return !gameA.Inbox.empty(); }));
    REQUIRE(gameA.Inbox[0] == Payload(20, 55));
    REQUIRE(PumpUntil(pump, [] { return true; }));
    REQUIRE(gameB.Inbox.empty());
}

TEST_CASE("Peers that are not admitted never reach the server", "[tunnel.admission]")
{
    FakeServer server;
    LoopbackNetwork network;

    // Only peer 100 is a "friend".
    TunnelHost host(network.Host(), {server.Socket.LocalPort(), 8}, [](PeerId aPeer) { return aPeer == 100; });

    auto friendLink = network.Connect(100);
    auto strangerLink = network.Connect(666);
    TunnelClient friendClient(*friendLink, LoopbackNetwork::kHostId);
    TunnelClient strangerClient(*strangerLink, LoopbackNetwork::kHostId);
    REQUIRE(friendClient.Start());
    REQUIRE(strangerClient.Start());

    FakeGame friendGame, strangerGame;
    const auto pump = [&]
    {
        host.Pump();
        friendClient.Pump();
        strangerClient.Pump();
        server.Poll();
    };

    REQUIRE(PumpUntil(pump, [&] { return strangerClient.IsClosed(); }));
    REQUIRE(host.Stats().PeersRejected == 1);
    REQUIRE(host.PeerCount() == 1);

    strangerGame.Socket.SendTo(Payload(8, 9).data(), 8, strangerClient.LocalPort());
    friendGame.Socket.SendTo(Payload(8, 4).data(), 8, friendClient.LocalPort());
    REQUIRE(PumpUntil(pump, [&] { return !server.Inbox.empty(); }));
    REQUIRE(PumpUntil(pump, [] { return true; }));

    // Exactly the friend's datagram arrived.
    REQUIRE(server.Inbox.size() == 1);
    REQUIRE(server.Inbox[0].Data == Payload(8, 4));
}

TEST_CASE("The peer limit is enforced", "[tunnel.limit]")
{
    FakeServer server;
    LoopbackNetwork network;
    TunnelHost host(network.Host(), {server.Socket.LocalPort(), 2}, AdmitAll());

    std::vector<std::unique_ptr<IPeerLink>> links;
    std::vector<std::unique_ptr<TunnelClient>> clients;
    for (PeerId id = 10; id < 13; ++id)
    {
        links.push_back(network.Connect(id));
        clients.push_back(std::make_unique<TunnelClient>(*links.back(), LoopbackNetwork::kHostId));
        REQUIRE(clients.back()->Start());
    }

    REQUIRE(PumpUntil(
        [&]
        {
            host.Pump();
            for (auto& client : clients)
                client->Pump();
        },
        [&] { return clients[2]->IsClosed(); }));

    REQUIRE(host.PeerCount() == 2);
    REQUIRE(host.Stats().PeersRejected == 1);
    REQUIRE_FALSE(clients[0]->IsClosed());
    REQUIRE_FALSE(clients[1]->IsClosed());
}

TEST_CASE("Hostile or malformed frames are dropped", "[tunnel.hostile]")
{
    FakeServer server;
    LoopbackNetwork network;
    TunnelHost host(network.Host(), {server.Socket.LocalPort(), 8}, AdmitAll());

    auto link = network.Connect(100);
    const auto pump = [&]
    {
        host.Pump();
        server.Poll();
    };
    REQUIRE(PumpUntil(pump, [&] { return host.PeerCount() == 1; }));

    const auto sendRaw = [&](const Bytes& aFrame) { link->Send(LoopbackNetwork::kHostId, aFrame.data(), aFrame.size()); };

    sendRaw({9, 1, 2, 3});                                   // unknown frame type
    sendRaw({1});                                            // frame with no payload
    sendRaw(Bytes(kMaxDatagram + 50, 1));                    // oversized
    sendRaw({static_cast<uint8_t>(FrameType::kData), 7, 7}); // the one good frame

    REQUIRE(PumpUntil(pump, [&] { return !server.Inbox.empty(); }));
    REQUIRE(PumpUntil(pump, [] { return true; }));
    REQUIRE(server.Inbox.size() == 1);
    REQUIRE(server.Inbox[0].Data == (Bytes{7, 7}));
    REQUIRE(host.Stats().DatagramsDropped == 3);
}

TEST_CASE("A player leaving frees their server-side socket", "[tunnel.leave]")
{
    FakeServer server;
    LoopbackNetwork network;
    TunnelHost host(network.Host(), {server.Socket.LocalPort(), 8}, AdmitAll());

    {
        auto link = network.Connect(100);
        TunnelClient client(*link, LoopbackNetwork::kHostId);
        REQUIRE(PumpUntil([&] { host.Pump(); client.Pump(); }, [&] { return host.PeerCount() == 1; }));
    } // link destroyed: the player left

    REQUIRE(PumpUntil([&] { host.Pump(); }, [&] { return host.PeerCount() == 0; }));
}
