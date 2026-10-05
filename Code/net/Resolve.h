#pragma once

// Turning "host", "host:port", "1.2.3.4:5" and "[::1]:5" into an address, without blocking the
// game thread on DNS.

#include <cstdint>
#include <memory>
#include <string>

namespace TrueMP::Net
{
struct Address
{
    bool Valid = false;
    bool IsV6 = false;
    uint8_t Bytes[16] = {}; // 4 bytes for IPv4, 16 for IPv6, network order
    uint16_t Port = 0;
};

// Splits an endpoint into host and port. A missing port becomes aDefaultPort. Returns false
// for an empty host, a malformed bracket, or a port outside 1-65535.
[[nodiscard]] bool SplitEndpoint(const std::string& aEndpoint, uint16_t aDefaultPort, std::string& aHost, uint16_t& aPort);

// A lookup running on its own thread. Poll Done() from the game thread; abandoning the job is
// safe, the thread finishes on its own.
class ResolveJob
{
public:
    // Starts resolving. Numeric addresses complete immediately without a thread.
    static std::shared_ptr<ResolveJob> Start(std::string aHost, uint16_t aPort);

    [[nodiscard]] bool Done() const noexcept;
    [[nodiscard]] Address Result() const;

private:
    struct State;
    explicit ResolveJob(std::shared_ptr<State> aState);
    std::shared_ptr<State> m_state;
};
} // namespace TrueMP::Net
