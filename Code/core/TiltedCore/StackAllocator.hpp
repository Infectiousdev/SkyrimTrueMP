#pragma once

#include "Allocator.hpp"

namespace TiltedPhoques
{
// Like ScratchAllocator, but the block lives inside the object, so it is free to create on the stack.
template <size_t Capacity> class StackAllocator final : public Allocator
{
public:
    StackAllocator() noexcept = default;

    TP_NOCOPYMOVE(StackAllocator);

    void* Allocate(size_t aSize) noexcept override
    {
        const size_t start = (m_used + 15) & ~size_t(15);
        if (start > Capacity || aSize > Capacity - start)
            return nullptr;

        m_used = start + aSize;
        return m_storage + start;
    }

    void Free(void*) noexcept override {}

    void Reset() noexcept { m_used = 0; }

    [[nodiscard]] size_t Used() const noexcept { return m_used; }

private:
    alignas(16) unsigned char m_storage[Capacity];
    size_t m_used = 0;
};
} // namespace TiltedPhoques
