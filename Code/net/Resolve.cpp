#include "Resolve.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace TrueMP::Net
{
namespace
{
void EnsureNetworking()
{
#ifdef _WIN32
    struct Init
    {
        Init()
        {
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
        }
    };
    static Init init;
#endif
}

// getaddrinfo can resolve a numeric address without touching the network; AI_NUMERICHOST makes
// that the only thing it will do.
Address Lookup(const std::string& aHost, uint16_t aPort, bool aNumericOnly)
{
    EnsureNetworking();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (aNumericOnly)
        hints.ai_flags = AI_NUMERICHOST;

    addrinfo* pResults = nullptr;
    Address address;
    if (getaddrinfo(aHost.c_str(), nullptr, &hints, &pResults) != 0 || !pResults)
        return address;

    // Prefer IPv4: many servers only listen there, and a name often has both.
    const addrinfo* pChosen = nullptr;
    for (const addrinfo* p = pResults; p; p = p->ai_next)
    {
        if (p->ai_family == AF_INET)
        {
            pChosen = p;
            break;
        }
        if (p->ai_family == AF_INET6 && !pChosen)
            pChosen = p;
    }

    if (pChosen && pChosen->ai_family == AF_INET)
    {
        std::memcpy(address.Bytes, &reinterpret_cast<const sockaddr_in*>(pChosen->ai_addr)->sin_addr, 4);
        address.Valid = true;
    }
    else if (pChosen && pChosen->ai_family == AF_INET6)
    {
        std::memcpy(address.Bytes, &reinterpret_cast<const sockaddr_in6*>(pChosen->ai_addr)->sin6_addr, 16);
        address.IsV6 = true;
        address.Valid = true;
    }

    address.Port = aPort;
    freeaddrinfo(pResults);
    return address;
}
} // namespace

bool SplitEndpoint(const std::string& aEndpoint, uint16_t aDefaultPort, std::string& aHost, uint16_t& aPort)
{
    std::string host = aEndpoint;
    std::string port;

    if (!host.empty() && host.front() == '[')
    {
        // [ipv6] or [ipv6]:port
        const auto close = host.find(']');
        if (close == std::string::npos)
            return false;
        const std::string rest = host.substr(close + 1);
        if (!rest.empty())
        {
            if (rest.front() != ':')
                return false;
            port = rest.substr(1);
        }
        host = host.substr(1, close - 1);
    }
    else if (const auto colon = host.find(':'); colon != std::string::npos)
    {
        // A second colon means a bare IPv6 address, which has no port.
        if (host.find(':', colon + 1) == std::string::npos)
        {
            port = host.substr(colon + 1);
            host = host.substr(0, colon);
        }
    }

    if (host.empty())
        return false;

    unsigned long value = aDefaultPort;
    if (!port.empty())
    {
        if (port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
            return false;
        value = std::stoul(port);
        if (value == 0 || value > 65535)
            return false;
    }

    aHost = host;
    aPort = static_cast<uint16_t>(value);
    return true;
}

struct ResolveJob::State
{
    std::atomic<bool> Done{false};
    mutable std::mutex Mutex;
    Address Result;
};

ResolveJob::ResolveJob(std::shared_ptr<State> aState)
    : m_state(std::move(aState))
{
}

std::shared_ptr<ResolveJob> ResolveJob::Start(std::string aHost, uint16_t aPort)
{
    auto state = std::make_shared<State>();

    // Numeric addresses need no lookup, so skip the thread.
    const Address numeric = Lookup(aHost, aPort, true);
    if (numeric.Valid)
    {
        state->Result = numeric;
        state->Done = true;
        return std::shared_ptr<ResolveJob>(new ResolveJob(state));
    }

    // The thread owns its own copy of the state, so abandoning the job is harmless.
    std::thread(
        [state, aHost, aPort]
        {
            const Address address = Lookup(aHost, aPort, false);
            std::lock_guard lock(state->Mutex);
            state->Result = address;
            state->Done = true;
        })
        .detach();

    return std::shared_ptr<ResolveJob>(new ResolveJob(state));
}

bool ResolveJob::Done() const noexcept
{
    return m_state->Done;
}

Address ResolveJob::Result() const
{
    std::lock_guard lock(m_state->Mutex);
    return m_state->Result;
}
} // namespace TrueMP::Net
