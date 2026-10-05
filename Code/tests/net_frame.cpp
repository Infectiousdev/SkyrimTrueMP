#include <catch2/catch.hpp>

#include <Clock.h>
#include <Frame.h>
#include <Resolve.h>

#include <snappy.h>

#include <chrono>
#include <thread>
#include <vector>

using namespace TrueMP::Net;

namespace
{
using Bytes = std::vector<uint8_t>;

Bytes Pattern(size_t aSize, uint32_t aSeed)
{
    // A deterministic stream that does not compress.
    Bytes bytes(aSize);
    uint32_t state = aSeed * 2654435761u + 1;
    for (auto& b : bytes)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        b = static_cast<uint8_t>(state);
    }
    return bytes;
}

// Runs a frame through decode and extract, as a receiver would.
bool RoundTrip(const Bytes& aPayload, Bytes& aOut, bool* apCompressed = nullptr)
{
    Bytes frameBytes;
    if (!EncodePayload(aPayload.data(), aPayload.size(), frameBytes, apCompressed))
        return false;

    DecodedFrame frame;
    if (!DecodeFrame(frameBytes.data(), frameBytes.size(), frame))
        return false;

    Bytes scratch;
    const uint8_t* pPayload = nullptr;
    size_t size = 0;
    if (!ExtractPayload(frame, scratch, pPayload, size))
        return false;

    aOut.assign(pPayload, pPayload + size);
    return true;
}
} // namespace

TEST_CASE("Frames carry payloads intact", "[net.frame]")
{
    Bytes out;
    bool compressed = false;

    SECTION("an empty payload")
    {
        REQUIRE(RoundTrip({}, out, &compressed));
        REQUIRE(out.empty());
        REQUIRE_FALSE(compressed);
    }

    SECTION("a small payload is sent as is")
    {
        const Bytes payload = {1, 2, 3, 4, 5};
        REQUIRE(RoundTrip(payload, out, &compressed));
        REQUIRE(out == payload);
        REQUIRE_FALSE(compressed);
    }

    SECTION("a compressible payload is compressed and restored exactly")
    {
        const Bytes payload(100'000, 'a');
        Bytes frameBytes;
        REQUIRE(EncodePayload(payload.data(), payload.size(), frameBytes, &compressed));
        REQUIRE(compressed);
        REQUIRE(frameBytes.size() < payload.size() / 10);
        REQUIRE(RoundTrip(payload, out));
        REQUIRE(out == payload);
    }

    SECTION("an incompressible payload is not made bigger")
    {
        const Bytes payload = Pattern(50'000, 7);
        Bytes frameBytes;
        REQUIRE(EncodePayload(payload.data(), payload.size(), frameBytes, &compressed));
        REQUIRE_FALSE(compressed);
        REQUIRE(frameBytes.size() == payload.size() + 1);
        REQUIRE(RoundTrip(payload, out));
        REQUIRE(out == payload);
    }

    SECTION("the largest payload passes and one more byte is refused")
    {
        const Bytes biggest = Pattern(kMaxPayload, 3);
        REQUIRE(RoundTrip(biggest, out));
        REQUIRE(out == biggest);

        const Bytes tooBig(kMaxPayload + 1, 0);
        Bytes frameBytes;
        REQUIRE_FALSE(EncodePayload(tooBig.data(), tooBig.size(), frameBytes));
    }
}

TEST_CASE("Frames from a hostile peer are rejected", "[net.frame]")
{
    DecodedFrame frame;
    Bytes scratch;
    const uint8_t* pPayload = nullptr;
    size_t size = 0;

    SECTION("empty and unknown types")
    {
        REQUIRE_FALSE(DecodeFrame(nullptr, 0, frame));
        const uint8_t empty[1] = {0};
        REQUIRE_FALSE(DecodeFrame(empty, 0, frame));
        const uint8_t unknown[3] = {9, 1, 2};
        REQUIRE_FALSE(DecodeFrame(unknown, sizeof(unknown), frame));
    }

    SECTION("a compressed body that claims to expand beyond the limit")
    {
        // Genuine snappy data that expands to well over kMaxPayload.
        const std::string huge(kMaxPayload * 4, 'x');
        std::string compressed;
        snappy::Compress(huge.data(), huge.size(), &compressed);

        Bytes frameBytes = {static_cast<uint8_t>(FrameType::kDataCompressed)};
        frameBytes.insert(frameBytes.end(), compressed.begin(), compressed.end());
        REQUIRE(DecodeFrame(frameBytes.data(), frameBytes.size(), frame));
        REQUIRE_FALSE(ExtractPayload(frame, scratch, pPayload, size));
    }

    SECTION("a compressed body that is corrupt")
    {
        Bytes frameBytes = {static_cast<uint8_t>(FrameType::kDataCompressed), 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
        REQUIRE(DecodeFrame(frameBytes.data(), frameBytes.size(), frame));
        REQUIRE_FALSE(ExtractPayload(frame, scratch, pPayload, size));
    }

    SECTION("a clock frame is not a payload")
    {
        const auto request = EncodeClockRequest(5);
        REQUIRE(DecodeFrame(request.data(), request.size(), frame));
        REQUIRE_FALSE(ExtractPayload(frame, scratch, pPayload, size));
    }
}

TEST_CASE("Clock frames round trip and reject wrong lengths", "[net.frame]")
{
    DecodedFrame frame;
    uint64_t a = 0, b = 0;

    const auto request = EncodeClockRequest(0x0102030405060708ull);
    REQUIRE(DecodeFrame(request.data(), request.size(), frame));
    REQUIRE(ParseClockRequest(frame, a));
    REQUIRE(a == 0x0102030405060708ull);

    const auto reply = EncodeClockReply(11, 0xfffffffffffffff0ull);
    REQUIRE(DecodeFrame(reply.data(), reply.size(), frame));
    REQUIRE(ParseClockReply(frame, a, b));
    REQUIRE(a == 11);
    REQUIRE(b == 0xfffffffffffffff0ull);

    // Truncated or padded frames are not accepted.
    REQUIRE(DecodeFrame(request.data(), request.size() - 1, frame));
    REQUIRE_FALSE(ParseClockRequest(frame, a));
    REQUIRE(DecodeFrame(reply.data(), reply.size() - 1, frame));
    REQUIRE_FALSE(ParseClockReply(frame, a, b));

    // A reply is not a request and vice versa.
    REQUIRE(DecodeFrame(reply.data(), reply.size(), frame));
    REQUIRE_FALSE(ParseClockRequest(frame, a));
}

TEST_CASE("Clock estimates server time from round trips", "[net.clock]")
{
    constexpr uint64_t kOffsetUs = 5'000'000; // the server is 5 s ahead of us

    SECTION("not synchronised until it has a sample")
    {
        Clock clock;
        REQUIRE_FALSE(clock.IsSynchronized());
        clock.AddSample(1'000'000, 1'000'000 + kOffsetUs + 10'000, 1'020'000);
        REQUIRE(clock.IsSynchronized());
    }

    SECTION("a symmetric trip gives the exact offset")
    {
        Clock clock;
        // Request leaves at 1.000 s, takes 10 ms each way; the server stamps its clock on arrival.
        clock.AddSample(1'000'000, 1'000'000 + 10'000 + kOffsetUs, 1'020'000);
        REQUIRE(clock.BestRoundTripUs() == 20'000);
        // At local 2.000 s the server reads 7.000 s.
        REQUIRE(clock.TickAt(2'000'000) == 7'000);
    }

    SECTION("the sample with the shortest round trip wins")
    {
        Clock clock;
        // A congested trip: 200 ms out, 20 ms back. Assuming symmetry misjudges the offset by 90 ms.
        clock.AddSample(1'000'000, 1'000'000 + 200'000 + kOffsetUs, 1'220'000);
        const uint64_t poor = clock.TickAt(2'000'000);
        // A clean trip.
        clock.AddSample(3'000'000, 3'000'000 + 10'000 + kOffsetUs, 3'020'000);
        const uint64_t good = clock.TickAt(4'000'000);

        REQUIRE(clock.BestRoundTripUs() == 20'000);
        REQUIRE(good == 9'000);   // exact
        REQUIRE(poor != 7'000);   // the congested sample alone was wrong
    }

    SECTION("an old good sample ages out of the window")
    {
        Clock clock;
        clock.AddSample(0, 10'000 + kOffsetUs, 20'000); // rtt 20 ms
        for (size_t i = 0; i < Clock::kWindow; ++i)
        {
            const uint64_t sent = 1'000'000 * (i + 1);
            clock.AddSample(sent, sent + 50'000 + kOffsetUs, sent + 100'000); // rtt 100 ms
        }
        REQUIRE(clock.BestRoundTripUs() == 100'000);
    }

    SECTION("a reply stamped before the request is ignored")
    {
        Clock clock;
        clock.AddSample(5'000'000, 1, 4'000'000);
        REQUIRE_FALSE(clock.IsSynchronized());
    }

    SECTION("time never runs backwards when the estimate improves")
    {
        Clock clock;
        // First estimate runs 90 ms fast.
        clock.AddSample(1'000'000, 1'000'000 + 200'000 + kOffsetUs, 1'220'000);
        const uint64_t before = clock.TickAt(2'000'000);
        // A better sample arrives and pulls the estimate back.
        clock.AddSample(2'000'000, 2'000'000 + 10'000 + kOffsetUs, 2'020'000);
        const uint64_t after = clock.TickAt(2'000'000);
        REQUIRE(after >= before);
        // ...and it keeps advancing once real time catches up.
        REQUIRE(clock.TickAt(3'000'000) > after);
    }

    SECTION("reset forgets everything")
    {
        Clock clock;
        clock.AddSample(0, kOffsetUs + 10'000, 20'000);
        clock.Reset();
        REQUIRE_FALSE(clock.IsSynchronized());
    }

    SECTION("converges despite jittery delays")
    {
        // Deterministic jitter: each direction takes 15 ms plus 0-30 ms of queueing.
        Clock clock;
        uint32_t state = 12345;
        const auto jitter = [&]
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<uint64_t>((state >> 8) % 30'000);
        };

        for (int i = 0; i < 40; ++i)
        {
            const uint64_t sent = 1'000'000ull * (i + 1);
            const uint64_t outbound = 15'000 + jitter();
            const uint64_t inbound = 15'000 + jitter();
            clock.AddSample(sent, sent + outbound + kOffsetUs, sent + outbound + inbound);
        }

        const uint64_t local = 50'000'000;
        const int64_t errorMs = static_cast<int64_t>(clock.TickAt(local)) - static_cast<int64_t>((local + kOffsetUs) / 1000);
        REQUIRE(std::abs(errorMs) <= 10);
    }
}

TEST_CASE("Endpoints are split into host and port", "[net.resolve]")
{
    std::string host;
    uint16_t port = 0;

    REQUIRE(SplitEndpoint("example.com", 10578, host, port));
    REQUIRE(host == "example.com");
    REQUIRE(port == 10578);

    REQUIRE(SplitEndpoint("example.com:4000", 10578, host, port));
    REQUIRE(host == "example.com");
    REQUIRE(port == 4000);

    REQUIRE(SplitEndpoint("1.2.3.4:65535", 1, host, port));
    REQUIRE(port == 65535);

    REQUIRE(SplitEndpoint("[::1]:5555", 1, host, port));
    REQUIRE(host == "::1");
    REQUIRE(port == 5555);

    REQUIRE(SplitEndpoint("[::1]", 7, host, port));
    REQUIRE(host == "::1");
    REQUIRE(port == 7);

    // A bare IPv6 address has no port to split off.
    REQUIRE(SplitEndpoint("fe80::1", 7, host, port));
    REQUIRE(host == "fe80::1");
    REQUIRE(port == 7);

    REQUIRE_FALSE(SplitEndpoint("", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint(":4000", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint("host:0", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint("host:65536", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint("host:12ab", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint("host:123456", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint("[::1", 1, host, port));
    REQUIRE_FALSE(SplitEndpoint("[::1]x", 1, host, port));
}

TEST_CASE("Resolving addresses", "[net.resolve]")
{
    const auto wait = [](const std::shared_ptr<ResolveJob>& aJob)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!aJob->Done() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return aJob->Done();
    };

    SECTION("a numeric IPv4 address is ready immediately")
    {
        const auto job = ResolveJob::Start("192.0.2.7", 4242);
        REQUIRE(job->Done());
        const Address address = job->Result();
        REQUIRE(address.Valid);
        REQUIRE_FALSE(address.IsV6);
        REQUIRE(address.Bytes[0] == 192);
        REQUIRE(address.Bytes[3] == 7);
        REQUIRE(address.Port == 4242);
    }

    SECTION("a numeric IPv6 address")
    {
        const auto job = ResolveJob::Start("::1", 4242);
        REQUIRE(job->Done());
        const Address address = job->Result();
        REQUIRE(address.Valid);
        REQUIRE(address.IsV6);
        REQUIRE(address.Bytes[15] == 1);
    }

    SECTION("localhost resolves to loopback")
    {
        const auto job = ResolveJob::Start("localhost", 4242);
        REQUIRE(wait(job));
        const Address address = job->Result();
        REQUIRE(address.Valid);
        if (!address.IsV6)
            REQUIRE(address.Bytes[0] == 127);
    }

    SECTION("a name that does not exist fails rather than hanging")
    {
        const auto job = ResolveJob::Start("no-such-host.invalid", 4242);
        REQUIRE(wait(job));
        REQUIRE_FALSE(job->Result().Valid);
    }
}
