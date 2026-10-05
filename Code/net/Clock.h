#pragma once

// A clock synchronised to the server, in milliseconds.
//
// The game stamps events with server time so that machines agree on ordering. The client asks
// the server for its time and measures the round trip: assuming the reply spent half the trip
// on the way back, the server's clock now reads (reply time + rtt/2). Network delay is noisy,
// and the sample with the shortest round trip is the least distorted by queueing, so the
// offset is taken from the best of the most recent samples.
//
// Time inputs are explicit so the maths can be tested without waiting on a real clock.

#include <array>
#include <cstddef>
#include <cstdint>

namespace TrueMP::Net
{
class Clock
{
public:
    static constexpr size_t kWindow = 8;

    void Reset() noexcept;

    // Records one measurement. All three times are microseconds: the client time the request
    // left, the server time in the reply, and the client time the reply arrived.
    void AddSample(uint64_t aClientSentUs, uint64_t aServerUs, uint64_t aClientReceivedUs) noexcept;

    [[nodiscard]] bool IsSynchronized() const noexcept { return m_count != 0; }

    // Server time in milliseconds at the given local time. Never decreases as the local time
    // increases, even when a new sample moves the estimate backwards.
    [[nodiscard]] uint64_t TickAt(uint64_t aLocalUs) const noexcept;

    // Server time in milliseconds now (steady clock).
    [[nodiscard]] uint64_t GetCurrentTick() const noexcept { return TickAt(LocalNowUs()); }

    // The round trip of the sample the estimate is based on, microseconds.
    [[nodiscard]] uint64_t BestRoundTripUs() const noexcept { return m_bestRttUs; }

    [[nodiscard]] static uint64_t LocalNowUs() noexcept;

private:
    struct Sample
    {
        int64_t OffsetUs = 0; // server time minus local time
        uint64_t RttUs = 0;
    };

    std::array<Sample, kWindow> m_samples{};
    size_t m_count = 0; // samples held, up to kWindow
    size_t m_next = 0;  // slot the next sample goes into
    int64_t m_offsetUs = 0;
    uint64_t m_bestRttUs = 0;
    mutable uint64_t m_lastTick = 0;
};
} // namespace TrueMP::Net
