#pragma once

#include "Clock.h"
#include "Net.h"

#include <memory>
#include <string>

namespace TrueMP::Net
{
// One connection to a game server. Derive and implement the four On* callbacks; call Update()
// regularly (every frame) from a single thread. All callbacks run inside Update() or Close().
class Client
{
public:
    enum EDisconnectReason
    {
        kTimeout,      // the server never answered, or went silent
        kLocalProblem, // something failed on this machine
        kKicked,       // the server closed the connection
        kCannotResolve,
        kAborted,      // Close() was called
        kNormal
    };

    // Bytes over the last full second. "Uncompressed" counts the game payloads before
    // compression, so the two together show what compression saves.
    struct Statistics
    {
        uint32_t SentBytes = 0;
        uint32_t RecvBytes = 0;
        uint32_t UncompressedSentBytes = 0;
        uint32_t UncompressedRecvBytes = 0;
    };

    struct ConnectionStatus
    {
        float OutPacketsPerSec = 0;
        float InPacketsPerSec = 0;
        float OutBytesPerSec = 0;
        float InBytesPerSec = 0;
        int PingMs = 0;
        float QualityLocal = 0; // 0..1, 1 is perfect
    };

    static constexpr uint16_t kDefaultPort = 10578;

    Client();
    virtual ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Starts connecting to "host", "host:port", "1.2.3.4:5" or "[::1]:5". Hostnames resolve in
    // the background. Returns false only if the endpoint is malformed or the library is
    // unavailable; later failures arrive as OnDisconnected().
    bool Connect(const std::string& aEndpoint);

    // Ends the connection (or an attempt) and calls OnDisconnected(kAborted) if there was one.
    void Close();

    void Update();

    // Sends one message. Returns false if not connected or the payload is larger than
    // kMaxPayload.
    bool Send(const void* apData, size_t aSize, EPacketFlags aFlags = kReliable) const;

    // True once connected and the clock is synchronised, which is when OnConnected() fires.
    [[nodiscard]] bool IsConnected() const noexcept;

    [[nodiscard]] ConnectionStatus GetConnectionStatus() const;
    [[nodiscard]] Statistics GetStatistics() const;
    [[nodiscard]] const Clock& GetClock() const noexcept;

    // How long to wait for the server before giving up, in milliseconds. 0 keeps the library
    // default (10 s). Applies to the next Connect().
    void SetConnectTimeout(int aMilliseconds) noexcept;

    virtual void OnConsume(const void* apData, uint32_t aSize) = 0;
    virtual void OnConnected() = 0;
    virtual void OnDisconnected(EDisconnectReason aReason) = 0;
    virtual void OnUpdate() = 0;

private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};
} // namespace TrueMP::Net
