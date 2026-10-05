#pragma once

// SkyrimTrueMP: minimal streaming SHA-256 (FIPS 180-4).
//
// Used to fingerprint plugin files for the mod manifest. It lives in the
// encoding library so the client, the server and the unit tests all share one
// implementation without pulling in a crypto dependency on the server.

#include <array>
#include <cstddef>
#include <cstdint>

namespace TrueMP
{
using Sha256Digest = std::array<uint8_t, 32>;

class Sha256
{
public:
    Sha256() noexcept { Reset(); }

    void Reset() noexcept;
    void Update(const void* apData, size_t aSize) noexcept;

    // Finishes the hash. The object must be Reset() before it is reused.
    [[nodiscard]] Sha256Digest Final() noexcept;

    [[nodiscard]] static Sha256Digest Hash(const void* apData, size_t aSize) noexcept;

private:
    void ProcessBlock(const uint8_t* apBlock) noexcept;

    std::array<uint32_t, 8> m_state{};
    std::array<uint8_t, 64> m_block{};
    size_t m_blockUsed = 0;
    uint64_t m_totalBytes = 0;
};
} // namespace TrueMP
