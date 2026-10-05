#pragma once

#include "Allocator.hpp"

namespace TiltedPhoques
{
// A bump allocator over one block. Individual frees do nothing; Reset() releases everything at
// once. Meant for short-lived work that is thrown away together, such as one message.
class ScratchAllocator final : public Allocator
{
public:
    explicit ScratchAllocator(size_t aCapacity) noexcept
        : m_pBlock(static_cast<unsigned char*>(Allocator::GetDefault()->Allocate(aCapacity)))
        , m_capacity(m_pBlock ? aCapacity : 0)
    {
    }

    ~ScratchAllocator() override { Allocator::GetDefault()->Free(m_pBlock); }

    TP_NOCOPYMOVE(ScratchAllocator);

    void* Allocate(size_t aSize) noexcept override
    {
        const size_t start = (m_used + 15) & ~size_t(15);
        if (start > m_capacity || aSize > m_capacity - start)
            return nullptr; // exhausted: the tagged path falls back to the default allocator

        m_used = start + aSize;
        return m_pBlock + start;
    }

    void Free(void*) noexcept override {}

    void Reset() noexcept { m_used = 0; }

    [[nodiscard]] size_t Used() const noexcept { return m_used; }
    [[nodiscard]] size_t Capacity() const noexcept { return m_capacity; }

private:
    unsigned char* m_pBlock;
    size_t m_capacity;
    size_t m_used = 0;
};
} // namespace TiltedPhoques
