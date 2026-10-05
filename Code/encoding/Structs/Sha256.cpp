#include "Sha256.h"

#include <algorithm>
#include <cstring>

namespace TrueMP
{
namespace
{
constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t RotR(uint32_t aValue, unsigned aBits) noexcept
{
    return (aValue >> aBits) | (aValue << (32 - aBits));
}
} // namespace

void Sha256::Reset() noexcept
{
    m_state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    m_blockUsed = 0;
    m_totalBytes = 0;
}

void Sha256::ProcessBlock(const uint8_t* apBlock) noexcept
{
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i)
    {
        w[i] = (uint32_t(apBlock[i * 4]) << 24) | (uint32_t(apBlock[i * 4 + 1]) << 16) | (uint32_t(apBlock[i * 4 + 2]) << 8) | uint32_t(apBlock[i * 4 + 3]);
    }
    for (size_t i = 16; i < 64; ++i)
    {
        const uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

    for (size_t i = 0; i < 64; ++i)
    {
        const uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::Update(const void* apData, size_t aSize) noexcept
{
    const auto* pBytes = static_cast<const uint8_t*>(apData);
    m_totalBytes += aSize;

    // Top up a partially filled block first.
    if (m_blockUsed != 0)
    {
        const size_t take = std::min(aSize, m_block.size() - m_blockUsed);
        std::memcpy(m_block.data() + m_blockUsed, pBytes, take);
        m_blockUsed += take;
        pBytes += take;
        aSize -= take;

        if (m_blockUsed < m_block.size())
            return;

        ProcessBlock(m_block.data());
        m_blockUsed = 0;
    }

    // Whole blocks straight from the input, no copy.
    while (aSize >= m_block.size())
    {
        ProcessBlock(pBytes);
        pBytes += m_block.size();
        aSize -= m_block.size();
    }

    if (aSize != 0)
    {
        std::memcpy(m_block.data(), pBytes, aSize);
        m_blockUsed = aSize;
    }
}

Sha256Digest Sha256::Final() noexcept
{
    const uint64_t bitLength = m_totalBytes * 8;

    // Padding: a single 1 bit, zeros up to 56 mod 64, then the 64-bit big-endian bit length.
    uint8_t padding[72] = {0x80};
    const size_t padLength = (m_blockUsed < 56) ? (56 - m_blockUsed) : (120 - m_blockUsed);
    Update(padding, padLength);

    uint8_t lengthBytes[8];
    for (size_t i = 0; i < 8; ++i)
        lengthBytes[i] = static_cast<uint8_t>(bitLength >> (56 - 8 * i));
    Update(lengthBytes, 8);

    Sha256Digest digest{};
    for (size_t i = 0; i < 8; ++i)
    {
        digest[i * 4] = static_cast<uint8_t>(m_state[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(m_state[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(m_state[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i]);
    }
    return digest;
}

Sha256Digest Sha256::Hash(const void* apData, size_t aSize) noexcept
{
    Sha256 hasher;
    hasher.Update(apData, aSize);
    return hasher.Final();
}
} // namespace TrueMP
