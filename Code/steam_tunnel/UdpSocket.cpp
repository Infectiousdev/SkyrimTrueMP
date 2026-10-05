#include "UdpSocket.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <utility>

namespace TrueMP::Steam
{
namespace
{
#ifdef _WIN32
struct WinsockInit
{
    WinsockInit()
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};

void EnsureNetworking()
{
    static WinsockInit init;
}
#else
void EnsureNetworking() {}
#endif

sockaddr_in LoopbackAddress(uint16_t aPort)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(aPort);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}
} // namespace

UdpSocket::~UdpSocket()
{
    Close();
}

UdpSocket::UdpSocket(UdpSocket&& aOther) noexcept
    : m_socket(std::exchange(aOther.m_socket, kInvalid))
    , m_localPort(std::exchange(aOther.m_localPort, uint16_t(0)))
    , m_connected(std::exchange(aOther.m_connected, false))
{
}

UdpSocket& UdpSocket::operator=(UdpSocket&& aOther) noexcept
{
    if (this != &aOther)
    {
        Close();
        m_socket = std::exchange(aOther.m_socket, kInvalid);
        m_localPort = std::exchange(aOther.m_localPort, uint16_t(0));
        m_connected = std::exchange(aOther.m_connected, false);
    }
    return *this;
}

bool UdpSocket::BindLoopback(uint16_t aPort)
{
    EnsureNetworking();
    Close();

    auto socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == static_cast<decltype(socketHandle)>(kInvalid))
        return false;
    m_socket = static_cast<Handle>(socketHandle);

#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonBlocking);
#else
    fcntl(m_socket, F_SETFL, fcntl(m_socket, F_GETFL, 0) | O_NONBLOCK);
#endif

    sockaddr_in address = LoopbackAddress(aPort);
    if (::bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        Close();
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int boundLength = sizeof(bound);
#else
    socklen_t boundLength = sizeof(bound);
#endif
    if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0)
    {
        Close();
        return false;
    }
    m_localPort = ntohs(bound.sin_port);
    return true;
}

bool UdpSocket::ConnectLoopback(uint16_t aPort)
{
    if (!IsOpen() && !BindLoopback(0))
        return false;

    sockaddr_in address = LoopbackAddress(aPort);
    if (::connect(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        return false;

    m_connected = true;
    return true;
}

void UdpSocket::Close() noexcept
{
    if (m_socket == kInvalid)
        return;

#ifdef _WIN32
    ::closesocket(m_socket);
#else
    ::close(m_socket);
#endif
    m_socket = kInvalid;
    m_localPort = 0;
    m_connected = false;
}

int UdpSocket::Receive(uint8_t* apBuffer, size_t aCapacity, uint16_t* apFromPort) noexcept
{
    if (!IsOpen())
        return -1;

    sockaddr_in from{};
#ifdef _WIN32
    int fromLength = sizeof(from);
    const int received = ::recvfrom(m_socket, reinterpret_cast<char*>(apBuffer), static_cast<int>(aCapacity), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
    if (received < 0)
    {
        const int error = WSAGetLastError();
        // WSAECONNRESET is how Windows reports an ICMP "port unreachable" for an earlier send; not fatal.
        return (error == WSAEWOULDBLOCK || error == WSAECONNRESET) ? 0 : -1;
    }
#else
    socklen_t fromLength = sizeof(from);
    const auto received = ::recvfrom(m_socket, apBuffer, aCapacity, 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
    if (received < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) ? 0 : -1;
#endif

    if (apFromPort)
        *apFromPort = ntohs(from.sin_port);
    return static_cast<int>(received);
}

bool UdpSocket::Send(const uint8_t* apData, size_t aSize) noexcept
{
    if (!IsOpen() || !m_connected)
        return false;

#ifdef _WIN32
    return ::send(m_socket, reinterpret_cast<const char*>(apData), static_cast<int>(aSize), 0) >= 0;
#else
    return ::send(m_socket, apData, aSize, MSG_NOSIGNAL) >= 0;
#endif
}

bool UdpSocket::SendTo(const uint8_t* apData, size_t aSize, uint16_t aPort) noexcept
{
    if (!IsOpen())
        return false;

    sockaddr_in address = LoopbackAddress(aPort);
#ifdef _WIN32
    return ::sendto(m_socket, reinterpret_cast<const char*>(apData), static_cast<int>(aSize), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) >= 0;
#else
    return ::sendto(m_socket, apData, aSize, MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&address), sizeof(address)) >= 0;
#endif
}
} // namespace TrueMP::Steam
