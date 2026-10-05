#include "Clock.h"

#include <chrono>

namespace TrueMP::Net
{
void Clock::Reset() noexcept
{
    *this = Clock{};
}

void Clock::AddSample(uint64_t aClientSentUs, uint64_t aServerUs, uint64_t aClientReceivedUs) noexcept
{
    // A reply that arrives "before" the request left means the local clock went wrong; ignore it.
    if (aClientReceivedUs < aClientSentUs)
        return;

    const uint64_t rtt = aClientReceivedUs - aClientSentUs;

    Sample sample;
    sample.RttUs = rtt;
    // At the moment the reply arrived, the server's clock read about aServerUs + rtt/2.
    sample.OffsetUs = static_cast<int64_t>(aServerUs) + static_cast<int64_t>(rtt / 2) - static_cast<int64_t>(aClientReceivedUs);

    m_samples[m_next] = sample;
    m_next = (m_next + 1) % kWindow;
    if (m_count < kWindow)
        ++m_count;

    // The least delayed sample is the most trustworthy.
    const Sample* pBest = &m_samples[0];
    for (size_t i = 1; i < m_count; ++i)
    {
        if (m_samples[i].RttUs < pBest->RttUs)
            pBest = &m_samples[i];
    }
    m_offsetUs = pBest->OffsetUs;
    m_bestRttUs = pBest->RttUs;
}

uint64_t Clock::TickAt(uint64_t aLocalUs) const noexcept
{
    const int64_t serverUs = static_cast<int64_t>(aLocalUs) + m_offsetUs;
    uint64_t tick = serverUs > 0 ? static_cast<uint64_t>(serverUs) / 1000 : 0;

    // Game logic assumes time only moves forward. If a better sample pulls the estimate back,
    // hold the last value until real time has caught up.
    if (tick < m_lastTick)
        tick = m_lastTick;
    m_lastTick = tick;
    return tick;
}

uint64_t Clock::LocalNowUs() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace TrueMP::Net
