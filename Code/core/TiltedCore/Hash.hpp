#pragma once

#include <cstddef>
#include <cstdint>

namespace TiltedPhoques
{
namespace FHash
{
namespace Detail
{
// Lookup table for CRC-64 with the ECMA-182 polynomial, processing the most significant bit first.
struct Crc64Table
{
    uint64_t Entries[256];

    constexpr Crc64Table()
        : Entries()
    {
        constexpr uint64_t kPolynomial = 0x42F0E1EBA9EA3693ull;
        for (uint32_t index = 0; index < 256; ++index)
        {
            uint64_t value = static_cast<uint64_t>(index) << 56;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & (uint64_t(1) << 63)) ? (value << 1) ^ kPolynomial : (value << 1);
            Entries[index] = value;
        }
    }
};
} // namespace Detail

// CRC-64/WE: ECMA-182 polynomial, initial value and final XOR of all ones, not reflected.
//
// This exact variant matters: the animation graph descriptors in the game data are keyed by this hash
// of a graph's variable names, so changing it would stop every descriptor from matching. The
// check value for the ASCII string "123456789" is 0x62EC59E3F1A4F00A.
inline uint64_t Crc64(const unsigned char* acpData, std::size_t aLength)
{
    static constexpr Detail::Crc64Table s_table;

    uint64_t crc = ~uint64_t(0);
    for (std::size_t i = 0; i < aLength; ++i)
        crc = s_table.Entries[static_cast<unsigned char>(crc >> 56) ^ acpData[i]] ^ (crc << 8);
    return ~crc;
}
} // namespace FHash
} // namespace TiltedPhoques
