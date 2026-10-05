#pragma once

#include "Net.h"

#include <memory>
#include <string>

namespace TrueMP::Net
{
// A game server. Derive and implement the On* callbacks, then call Update() in a loop: each call
// services the network, runs OnUpdate() when a tick is due, and otherwise sleeps briefly, so a
// bare `while (IsListening()) Update();` neither spins nor misses ticks. Single threaded; every
// callback runs inside Update().
class Server
{
public:
    // Why a connection ended.
    enum EDisconnectReason : int
    {
        Unknown,
        Quit,          // the player closed the connection
        Kicked,        // Kick() was called
        Banned,
        BadConnection, // the connection failed
        TimedOut       // the player went silent
    };

    // Connections beyond this are refused before they reach the game, bounding what a flood of
    // half-open connections can cost.
    static constexpr size_t kMaxConnections = 64;

    Server();
    virtual ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Starts listening on aPort and ticking aTickRate times per second. Returns false if the
    // port cannot be bound (for example it is in use) or the library is unavailable.
    bool Host(uint16_t aPort, uint32_t aTickRate, bool aEnableDualStackIP = true);
    void Close();

    void Update();

    // Messages to one connection or to all of them. Return false if the payload is too large
    // or (for Send) the connection is gone.
    bool Send(ConnectionId_t aConnectionId, const void* apData, size_t aSize, EPacketFlags aFlags = kReliable) const;
    bool SendToAll(const void* apData, size_t aSize, EPacketFlags aFlags = kReliable) const;

    // Removes a client. Messages already queued to it are still delivered first. OnDisconnection()
    // follows from the next Update(), not from inside this call, so a message handler may kick the
    // very player whose message it is handling.
    void Kick(ConnectionId_t aConnectionId);

    [[nodiscard]] uint16_t GetPort() const noexcept;
    [[nodiscard]] bool IsListening() const noexcept;
    [[nodiscard]] uint32_t GetClientCount() const noexcept;
    [[nodiscard]] uint32_t GetTickRate() const noexcept;

    // Milliseconds since Host(). This is the time clients synchronise to.
    [[nodiscard]] uint64_t GetTick() const noexcept;

    // The remote IP address without the port, or an empty string for an unknown connection.
    [[nodiscard]] std::string GetRemoteAddress(ConnectionId_t aConnectionId) const;
    [[nodiscard]] bool IsAlive(ConnectionId_t aConnectionId) const;

    virtual void OnUpdate() = 0;
    virtual void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) = 0;
    virtual void OnConnection(ConnectionId_t aConnectionId) = 0;
    virtual void OnDisconnection(ConnectionId_t aConnectionId, EDisconnectReason aReason) = 0;

private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};
} // namespace TrueMP::Net
