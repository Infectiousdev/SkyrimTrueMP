#pragma once

// A byte buffer with a bit-level reader and writer.
//
// Network messages are packed tightly: a flag costs one bit, a small number only as many bits as
// it needs. Bits fill each byte from the least significant end, and a multi-bit value is written
// least significant bit first, so the stream is simply bit 0, bit 1, bit 2, ... of the buffer.
//
// Reads and writes never go outside the buffer: they fail (return false) instead.

#include "Allocator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace TiltedPhoques
{
struct Buffer : AllocatorCompatible
{
    Buffer() noexcept = default;

    explicit Buffer(size_t aSize) noexcept { Reserve(aSize); }

    Buffer(const uint8_t* apData, size_t aSize) noexcept
    {
        if (Reserve(aSize) && aSize != 0)
            std::memcpy(m_pData, apData, aSize);
    }

    Buffer(const Buffer& acBuffer) noexcept
    {
        if (Reserve(acBuffer.m_size) && m_size != 0)
            std::memcpy(m_pData, acBuffer.m_pData, m_size);
    }

    Buffer(Buffer&& aBuffer) noexcept
        : m_pData(aBuffer.m_pData)
        , m_size(aBuffer.m_size)
    {
        aBuffer.m_pData = nullptr;
        aBuffer.m_size = 0;
    }

    virtual ~Buffer() { Release(); }

    Buffer& operator=(const Buffer& acBuffer) noexcept
    {
        if (this != &acBuffer)
        {
            Release();
            if (Reserve(acBuffer.m_size) && m_size != 0)
                std::memcpy(m_pData, acBuffer.m_pData, m_size);
        }
        return *this;
    }

    Buffer& operator=(Buffer&& aBuffer) noexcept
    {
        if (this != &aBuffer)
        {
            Release();
            m_pData = aBuffer.m_pData;
            m_size = aBuffer.m_size;
            aBuffer.m_pData = nullptr;
            aBuffer.m_size = 0;
        }
        return *this;
    }

    uint8_t operator[](size_t aIndex) const noexcept { return m_pData[aIndex]; }
    uint8_t& operator[](size_t aIndex) noexcept { return m_pData[aIndex]; }

    [[nodiscard]] size_t GetSize() const noexcept { return m_size; }

    // Changes the size, keeping the contents that still fit. New bytes are zero. Returns false
    // (leaving the buffer unchanged) if memory cannot be obtained.
    bool Resize(size_t aSize) noexcept
    {
        if (aSize == m_size)
            return true;

        uint8_t* pNew = aSize ? static_cast<uint8_t*>(Allocator::AllocateTagged(aSize)) : nullptr;
        if (aSize && !pNew)
            return false;

        if (pNew)
        {
            const size_t keep = std::min(aSize, m_size);
            if (keep)
                std::memcpy(pNew, m_pData, keep);
            std::memset(pNew + keep, 0, aSize - keep);
        }

        Release();
        m_pData = pNew;
        m_size = aSize;
        return true;
    }

    [[nodiscard]] const uint8_t* GetData() const noexcept { return m_pData; }
    [[nodiscard]] uint8_t* GetWriteData() const noexcept { return m_pData; }

    // A position in a buffer, in bits.
    struct Cursor
    {
        explicit Cursor(Buffer* apBuffer) noexcept
            : m_bitPosition(0)
            , m_pBuffer(apBuffer)
        {
        }

        void Reset() noexcept { m_bitPosition = 0; }
        [[nodiscard]] bool Eof() const noexcept { return m_bitPosition >= m_pBuffer->GetSize() * 8; }

        // Moves by whole bytes.
        void Advance(size_t aByteCount) noexcept { m_bitPosition += aByteCount * 8; }
        void Reverse(size_t aByteCount) noexcept { m_bitPosition = (aByteCount * 8 > m_bitPosition) ? 0 : m_bitPosition - aByteCount * 8; }

        // Bytes touched so far, counting a partly used byte as a whole one.
        [[nodiscard]] size_t Size() const noexcept { return (m_bitPosition + 7) / 8; }
        [[nodiscard]] size_t GetBytePosition() const noexcept { return m_bitPosition / 8; }
        [[nodiscard]] size_t GetBitPosition() const noexcept { return m_bitPosition; }

        [[nodiscard]] Buffer* GetBuffer() const noexcept { return m_pBuffer; }
        [[nodiscard]] uint8_t* GetDataAtPosition() const noexcept { return m_pBuffer->GetWriteData() + GetBytePosition(); }

    protected:
        [[nodiscard]] bool Fits(size_t aBits) const noexcept { return aBits <= m_pBuffer->GetSize() * 8 - std::min(m_bitPosition, m_pBuffer->GetSize() * 8); }

        size_t m_bitPosition;
        Buffer* m_pBuffer;
    };

    struct Reader : Cursor
    {
        explicit Reader(Buffer* apBuffer) noexcept
            : Cursor(apBuffer)
        {
        }

        // Reads aCount (0-64) bits into aDestination. On failure nothing is consumed and
        // aDestination is zero.
        bool ReadBits(uint64_t& aDestination, size_t aCount) noexcept
        {
            aDestination = 0;
            if (aCount > 64 || !Fits(aCount))
                return false;

            const uint8_t* pData = m_pBuffer->GetData();
            size_t position = m_bitPosition;
            size_t produced = 0;
            while (produced < aCount)
            {
                const size_t offset = position & 7;
                const size_t take = std::min<size_t>(8 - offset, aCount - produced);
                const uint64_t bits = (static_cast<uint64_t>(pData[position >> 3]) >> offset) & ((uint64_t(1) << take) - 1);
                aDestination |= bits << produced;
                position += take;
                produced += take;
            }

            m_bitPosition = position;
            return true;
        }

        // Reads whole bytes, starting at any bit position.
        bool ReadBytes(uint8_t* apDestination, size_t aCount) noexcept
        {
            if (aCount > (m_pBuffer->GetSize() * 8) / 8 || !Fits(aCount * 8))
                return false;

            if ((m_bitPosition & 7) == 0)
            {
                if (aCount)
                    std::memcpy(apDestination, m_pBuffer->GetData() + (m_bitPosition >> 3), aCount);
                m_bitPosition += aCount * 8;
                return true;
            }

            for (size_t i = 0; i < aCount; ++i)
            {
                uint64_t byte = 0;
                ReadBits(byte, 8);
                apDestination[i] = static_cast<uint8_t>(byte);
            }
            return true;
        }
    };

    struct Writer : Cursor
    {
        explicit Writer(Buffer* apBuffer) noexcept
            : Cursor(apBuffer)
        {
        }

        // Writes the low aCount (0-64) bits of aData. Fails, writing nothing, if they do not fit.
        bool WriteBits(uint64_t aData, size_t aCount) noexcept
        {
            if (aCount > 64 || !Fits(aCount))
                return false;

            if (aCount < 64)
                aData &= (uint64_t(1) << aCount) - 1;

            uint8_t* pData = m_pBuffer->GetWriteData();
            size_t position = m_bitPosition;
            size_t remaining = aCount;
            while (remaining)
            {
                const size_t offset = position & 7;
                const size_t take = std::min<size_t>(8 - offset, remaining);
                const auto mask = static_cast<uint8_t>(((1u << take) - 1) << offset);
                uint8_t& rByte = pData[position >> 3];
                rByte = static_cast<uint8_t>((rByte & ~mask) | (static_cast<uint8_t>(aData << offset) & mask));
                aData >>= take;
                position += take;
                remaining -= take;
            }

            m_bitPosition = position;
            return true;
        }

        bool WriteBytes(const uint8_t* apSource, size_t aCount) noexcept
        {
            if (aCount > m_pBuffer->GetSize() || !Fits(aCount * 8))
                return false;

            if ((m_bitPosition & 7) == 0)
            {
                if (aCount)
                    std::memcpy(m_pBuffer->GetWriteData() + (m_bitPosition >> 3), apSource, aCount);
                m_bitPosition += aCount * 8;
                return true;
            }

            for (size_t i = 0; i < aCount; ++i)
                WriteBits(apSource[i], 8);
            return true;
        }
    };

protected:
    uint8_t* m_pData = nullptr;
    size_t m_size = 0;

private:
    bool Reserve(size_t aSize) noexcept
    {
        m_size = 0;
        m_pData = nullptr;
        if (aSize == 0)
            return true;

        m_pData = static_cast<uint8_t*>(Allocator::AllocateTagged(aSize));
        if (!m_pData)
            return false;

        std::memset(m_pData, 0, aSize);
        m_size = aSize;
        return true;
    }

    void Release() noexcept
    {
        // A view over someone else's memory (ViewBuffer) sets these to null before we get here.
        Allocator::FreeTagged(m_pData);
        m_pData = nullptr;
        m_size = 0;
    }
};
} // namespace TiltedPhoques
