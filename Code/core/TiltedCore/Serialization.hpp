#pragma once

// Encodings for the basic types, on top of Buffer's bit reader and writer.
//
//   bool            1 bit
//   varint          7 bits per byte, least significant group first, high bit = "more follows"
//   string          varint length, then the bytes
//   float / double  the IEEE-754 bit pattern, 32 / 64 bits
//
// Readers are written for hostile input: they never read past the buffer, never loop forever, and
// a length that claims more than the buffer holds yields an empty result, not a giant allocation.

#include "Buffer.hpp"
#include "Stl.hpp"

#include <cstring>

namespace TiltedPhoques
{
struct Serialization
{
    static bool ReadBool(Buffer::Reader& aReader) noexcept
    {
        uint64_t bit = 0;
        aReader.ReadBits(bit, 1);
        return bit != 0;
    }

    static void WriteBool(Buffer::Writer& aWriter, bool aValue) noexcept { aWriter.WriteBits(aValue ? 1 : 0, 1); }

    static uint64_t ReadVarInt(Buffer::Reader& aReader) noexcept
    {
        uint64_t value = 0;
        // A 64-bit value needs at most 10 groups; anything longer is malformed and is cut off.
        for (unsigned shift = 0; shift < 70; shift += 7)
        {
            uint64_t group = 0;
            if (!aReader.ReadBits(group, 8))
                return value;

            if (shift < 64)
                value |= (group & 0x7f) << shift;
            if ((group & 0x80) == 0)
                break;
        }
        return value;
    }

    static void WriteVarInt(Buffer::Writer& aWriter, uint64_t aValue) noexcept
    {
        while (aValue >= 0x80)
        {
            aWriter.WriteBits((aValue & 0x7f) | 0x80, 8);
            aValue >>= 7;
        }
        aWriter.WriteBits(aValue, 8);
    }

    static String ReadString(Buffer::Reader& aReader)
    {
        const uint64_t length = ReadVarInt(aReader);

        // Each character is 8 bits, so a longer claim than what is left is nonsense.
        const size_t remainingBytes = aReader.GetBuffer()->GetSize() - std::min(aReader.GetBytePosition(), aReader.GetBuffer()->GetSize());
        if (length == 0 || length > remainingBytes)
            return String();

        String text(static_cast<size_t>(length), '\0');
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(text.data()), text.size());
        return text;
    }

    static void WriteString(Buffer::Writer& aWriter, const String& acString) noexcept
    {
        WriteVarInt(aWriter, acString.size());
        aWriter.WriteBytes(reinterpret_cast<const uint8_t*>(acString.data()), acString.size());
    }

    static double ReadDouble(Buffer::Reader& aReader) noexcept
    {
        uint64_t bits = 0;
        aReader.ReadBits(bits, 64);
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    static void WriteDouble(Buffer::Writer& aWriter, double aValue) noexcept
    {
        uint64_t bits;
        std::memcpy(&bits, &aValue, sizeof(bits));
        aWriter.WriteBits(bits, 64);
    }

    static float ReadFloat(Buffer::Reader& aReader) noexcept
    {
        uint64_t bits = 0;
        aReader.ReadBits(bits, 32);
        const auto narrow = static_cast<uint32_t>(bits);
        float value;
        std::memcpy(&value, &narrow, sizeof(value));
        return value;
    }

    static void WriteFloat(Buffer::Writer& aWriter, float aValue) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &aValue, sizeof(bits));
        aWriter.WriteBits(bits, 32);
    }
};
} // namespace TiltedPhoques
