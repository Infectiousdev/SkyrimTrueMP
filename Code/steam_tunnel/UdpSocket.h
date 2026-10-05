#pragma once

// SkyrimTrueMP: non-blocking UDP socket bound to / talking to 127.0.0.1 only.

#include <cstddef>
#include <cstdint>

namespace TrueMP::Steam
{
class UdpSocket
{
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& aOther) noexcept;
    UdpSocket& operator=(UdpSocket&& aOther) noexcept;

    // Binds to 127.0.0.1:aPort (0 picks a free port). Returns false on failure.
    bool BindLoopback(uint16_t aPort = 0);

    // Fixes the remote end to 127.0.0.1:aPort so Send() needs no address.
    bool ConnectLoopback(uint16_t aPort);

    [[nodiscard]] bool IsOpen() const noexcept { return m_socket != kInvalid; }
    [[nodiscard]] uint16_t LocalPort() const noexcept { return m_localPort; }
    void Close() noexcept;

    // Receive one datagram. Returns its size, 0 if nothing is waiting, -1 on error.
    // aFromPort is the sender's port when it is not null.
    int Receive(uint8_t* apBuffer, size_t aCapacity, uint16_t* apFromPort = nullptr) noexcept;

    // Send to the connected remote, or to 127.0.0.1:aPort when the socket is not connected.
    bool Send(const uint8_t* apData, size_t aSize) noexcept;
    bool SendTo(const uint8_t* apData, size_t aSize, uint16_t aPort) noexcept;

private:
#ifdef _WIN32
    using Handle = uintptr_t;
    static constexpr Handle kInvalid = ~Handle(0);
#else
    using Handle = int;
    static constexpr Handle kInvalid = -1;
#endif

    Handle m_socket = kInvalid;
    uint16_t m_localPort = 0;
    bool m_connected = false;
};
} // namespace TrueMP::Steam
