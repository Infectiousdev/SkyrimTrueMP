#include "Frame.h"

#include <snappy.h>

namespace TrueMP::Net
{
namespace
{
void PutU64(uint8_t* apOut, uint64_t aValue)
{
    for (int i = 0; i < 8; ++i)
        apOut[i] = static_cast<uint8_t>(aValue >> (8 * i));
}

uint64_t GetU64(const uint8_t* apIn)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(apIn[i]) << (8 * i);
    return value;
}
} // namespace

bool EncodePayload(const void* apData, size_t aSize, std::vector<uint8_t>& aOut, bool* apCompressed)
{
    if (aSize > kMaxPayload)
        return false;

    const char* pData = static_cast<const char*>(apData);
    if (apCompressed)
        *apCompressed = false;

    if (aSize >= kMinCompressSize)
    {
        aOut.resize(1 + snappy::MaxCompressedLength(aSize));
        size_t compressedSize = 0;
        snappy::RawCompress(pData, aSize, reinterpret_cast<char*>(aOut.data() + 1), &compressedSize);

        // Only worth the decompression cost if it saves a meaningful amount.
        if (compressedSize + 8 < aSize)
        {
            aOut[0] = static_cast<uint8_t>(FrameType::kDataCompressed);
            aOut.resize(1 + compressedSize);
            if (apCompressed)
                *apCompressed = true;
            return true;
        }
    }

    aOut.resize(1 + aSize);
    aOut[0] = static_cast<uint8_t>(FrameType::kData);
    if (aSize != 0)
        std::copy(pData, pData + aSize, reinterpret_cast<char*>(aOut.data() + 1));
    return true;
}

std::array<uint8_t, 9> EncodeClockRequest(uint64_t aClientTimeUs)
{
    std::array<uint8_t, 9> frame{};
    frame[0] = static_cast<uint8_t>(FrameType::kClockRequest);
    PutU64(frame.data() + 1, aClientTimeUs);
    return frame;
}

std::array<uint8_t, 17> EncodeClockReply(uint64_t aClientTimeUs, uint64_t aServerTimeUs)
{
    std::array<uint8_t, 17> frame{};
    frame[0] = static_cast<uint8_t>(FrameType::kClockReply);
    PutU64(frame.data() + 1, aClientTimeUs);
    PutU64(frame.data() + 9, aServerTimeUs);
    return frame;
}

bool DecodeFrame(const void* apData, size_t aSize, DecodedFrame& aFrame)
{
    if (!apData || aSize == 0)
        return false;

    const auto* pBytes = static_cast<const uint8_t*>(apData);
    if (pBytes[0] > static_cast<uint8_t>(FrameType::kClockReply))
        return false;

    aFrame.Type = static_cast<FrameType>(pBytes[0]);
    aFrame.Body = pBytes + 1;
    aFrame.BodySize = aSize - 1;
    return true;
}

bool ExtractPayload(const DecodedFrame& aFrame, std::vector<uint8_t>& aScratch, const uint8_t*& aPayload, size_t& aPayloadSize)
{
    if (aFrame.Type == FrameType::kData)
    {
        if (aFrame.BodySize > kMaxPayload)
            return false;
        aPayload = aFrame.Body;
        aPayloadSize = aFrame.BodySize;
        return true;
    }

    if (aFrame.Type != FrameType::kDataCompressed)
        return false;

    size_t expandedSize = 0;
    if (!snappy::GetUncompressedLength(reinterpret_cast<const char*>(aFrame.Body), aFrame.BodySize, &expandedSize) || expandedSize > kMaxPayload)
        return false;

    aScratch.resize(expandedSize);
    if (!snappy::RawUncompress(reinterpret_cast<const char*>(aFrame.Body), aFrame.BodySize, reinterpret_cast<char*>(aScratch.data())))
        return false;

    aPayload = aScratch.data();
    aPayloadSize = expandedSize;
    return true;
}

bool ParseClockRequest(const DecodedFrame& aFrame, uint64_t& aClientTimeUs)
{
    if (aFrame.Type != FrameType::kClockRequest || aFrame.BodySize != 8)
        return false;
    aClientTimeUs = GetU64(aFrame.Body);
    return true;
}

bool ParseClockReply(const DecodedFrame& aFrame, uint64_t& aClientTimeUs, uint64_t& aServerTimeUs)
{
    if (aFrame.Type != FrameType::kClockReply || aFrame.BodySize != 16)
        return false;
    aClientTimeUs = GetU64(aFrame.Body);
    aServerTimeUs = GetU64(aFrame.Body + 8);
    return true;
}
} // namespace TrueMP::Net
