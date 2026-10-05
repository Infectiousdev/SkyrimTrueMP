#pragma once

// Wire format. Every message on a connection is one frame: a type byte, then a body.
//
//   kData            body = the game payload, as sent
//   kDataCompressed  body = snappy-compressed game payload
//   kClockRequest    body = u64 client time (us)
//   kClockReply      body = u64 echoed client time (us), u64 server time (us)
//
// Integers are little-endian. This file has no network or platform dependencies so the
// codec can be tested in isolation.

#include "Net.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TrueMP::Net
{
enum class FrameType : uint8_t
{
    kData = 0,
    kDataCompressed = 1,
    kClockRequest = 2,
    kClockReply = 3
};

struct DecodedFrame
{
    FrameType Type{};
    const uint8_t* Body = nullptr;
    size_t BodySize = 0;
};

// Payloads smaller than this are never worth compressing.
inline constexpr size_t kMinCompressSize = 128;

// Builds the frame for a game payload into aOut, compressing it only when that makes it
// smaller. Returns false if the payload exceeds kMaxPayload.
[[nodiscard]] bool EncodePayload(const void* apData, size_t aSize, std::vector<uint8_t>& aOut, bool* apCompressed = nullptr);

[[nodiscard]] std::array<uint8_t, 9> EncodeClockRequest(uint64_t aClientTimeUs);
[[nodiscard]] std::array<uint8_t, 17> EncodeClockReply(uint64_t aClientTimeUs, uint64_t aServerTimeUs);

// Splits a received message into type and body. Returns false for an empty message or an
// unknown type.
[[nodiscard]] bool DecodeFrame(const void* apData, size_t aSize, DecodedFrame& aFrame);

// Gets the game payload out of a kData or kDataCompressed frame. A compressed body is expanded
// into aScratch, which then owns the bytes aPayload points at. Refuses data that would expand
// beyond kMaxPayload, so a hostile peer cannot make us allocate without bound.
[[nodiscard]] bool ExtractPayload(const DecodedFrame& aFrame, std::vector<uint8_t>& aScratch, const uint8_t*& aPayload, size_t& aPayloadSize);

[[nodiscard]] bool ParseClockRequest(const DecodedFrame& aFrame, uint64_t& aClientTimeUs);
[[nodiscard]] bool ParseClockReply(const DecodedFrame& aFrame, uint64_t& aClientTimeUs, uint64_t& aServerTimeUs);
} // namespace TrueMP::Net
