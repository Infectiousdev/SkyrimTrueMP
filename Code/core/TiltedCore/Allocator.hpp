#pragma once

// Allocators.
//
// The game code allocates through a per-thread "current" allocator so hot paths (building one
// network message, formatting a debug view) can bump-allocate from a scratch buffer and drop it all
// at once, instead of paying for the general heap.
//
//   StackAllocator<4096> scratch;
//   ScopedAllocator scope{scratch};   // allocations on this thread now come from `scratch`
//   Vector<int> numbers;              // ...including this vector's storage
//
// The one hazard is an allocation outliving the allocator that served it. To make that survivable,
// anything allocated through AllocateTagged() (containers via StlAllocator, and classes derived from
// AllocatorCompatible) remembers which allocator it came from, and is always handed back to that
// allocator no matter which one is current when it is freed.

#include "Meta.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mimalloc.h>
#include <new>
#include <type_traits>

namespace TiltedPhoques
{
class Allocator
{
public:
    virtual ~Allocator() = default;

    // Returns nullptr when out of memory. The block is aligned for any fundamental type.
    virtual void* Allocate(size_t aSize) noexcept = 0;
    virtual void Free(void* apBlock) noexcept = 0;

    // The process-wide allocator (mimalloc). Global operator new/delete use this directly.
    static Allocator* GetDefault() noexcept;

    // The allocator new allocations on this thread come from: the default unless a ScopedAllocator
    // (or Set) says otherwise.
    static Allocator* Get() noexcept;

    // Makes aAllocator current for this thread (nullptr restores the default) and returns the previous one.
    static Allocator* Set(Allocator* apAllocator) noexcept;

    // Allocation that records its origin, so the block can be freed correctly from anywhere. Falls
    // back to the default allocator if the current one is exhausted. Returns nullptr only if both fail.
    static void* AllocateTagged(size_t aSize, size_t aAlignment = alignof(std::max_align_t)) noexcept;
    static void FreeTagged(void* apBlock) noexcept;

private:
    // Sits immediately before the pointer handed to the caller.
    struct Tag
    {
        Allocator* Owner;
        void* Raw; // what the owner actually returned, to give back on free
    };
    static_assert(sizeof(Tag) <= 16);

    static Allocator*& Current() noexcept
    {
        static thread_local Allocator* s_pCurrent = nullptr;
        return s_pCurrent;
    }

};

// Sets the current allocator for this thread for the lifetime of the object.
class ScopedAllocator
{
public:
    explicit ScopedAllocator(Allocator* apAllocator) noexcept
        : m_pPrevious(Allocator::Set(apAllocator))
    {
    }

    explicit ScopedAllocator(Allocator& aAllocator) noexcept
        : ScopedAllocator(&aAllocator)
    {
    }

    ~ScopedAllocator() { Allocator::Set(m_pPrevious); }

    TP_NOCOPYMOVE(ScopedAllocator);

private:
    Allocator* m_pPrevious;
};

// Base for types that should allocate through the current allocator when created with `new`.
struct AllocatorCompatible
{
    static void* operator new(size_t aSize)
    {
        if (void* pBlock = Allocator::AllocateTagged(aSize))
            return pBlock;
        throw std::bad_alloc();
    }
    static void* operator new[](size_t aSize) { return operator new(aSize); }

    static void operator delete(void* apBlock) noexcept { Allocator::FreeTagged(apBlock); }
    static void operator delete[](void* apBlock) noexcept { Allocator::FreeTagged(apBlock); }

    // Declaring operator new in a class hides the global placement forms; put them back.
    static void* operator new(size_t, void* apWhere) noexcept { return apWhere; }
    static void operator delete(void*, void*) noexcept {}
};

// Standard-library allocator that draws from the current allocator. All instances are
// interchangeable because every block carries its own owner.
template <class T> struct StlAllocator
{
    using value_type = T;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    StlAllocator() noexcept = default;
    template <class U> StlAllocator(const StlAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(size_t aCount)
    {
        if (aCount > static_cast<size_t>(-1) / sizeof(T))
            throw std::bad_array_new_length();
        if (void* pBlock = Allocator::AllocateTagged(aCount * sizeof(T), alignof(T)))
            return static_cast<T*>(pBlock);
        throw std::bad_alloc();
    }

    void deallocate(T* apBlock, size_t) noexcept { Allocator::FreeTagged(apBlock); }

    template <class U> bool operator==(const StlAllocator<U>&) const noexcept { return true; }
    template <class U> bool operator!=(const StlAllocator<U>&) const noexcept { return false; }
};
} // namespace TiltedPhoques

namespace TiltedPhoques
{
// The general-purpose allocator: mimalloc.
class MimallocAllocator final : public Allocator
{
public:
    void* Allocate(size_t aSize) noexcept override { return mi_malloc(aSize); }
    void Free(void* apBlock) noexcept override { mi_free(apBlock); }
};

inline Allocator* Allocator::GetDefault() noexcept
{
    // Never destroyed: blocks may be freed during static destruction, after a normal static would be gone.
    alignas(MimallocAllocator) static unsigned char s_storage[sizeof(MimallocAllocator)];
    static Allocator* s_pDefault = new (s_storage) MimallocAllocator();
    return s_pDefault;
}

inline Allocator* Allocator::Get() noexcept
{
    Allocator* pCurrent = Current();
    return pCurrent ? pCurrent : GetDefault();
}

inline Allocator* Allocator::Set(Allocator* apAllocator) noexcept
{
    Allocator*& rpCurrent = Current();
    Allocator* pPrevious = rpCurrent ? rpCurrent : GetDefault();
    rpCurrent = (apAllocator == GetDefault()) ? nullptr : apAllocator;
    return pPrevious;
}

inline void* Allocator::AllocateTagged(size_t aSize, size_t aAlignment) noexcept
{
    // The user pointer sits after a Tag and is aligned to at least 16 bytes, more if asked.
    if (aAlignment < 16)
        aAlignment = 16;

    const size_t total = aSize + aAlignment + sizeof(Tag);
    if (total < aSize)
        return nullptr; // overflow

    Allocator* pOwner = Get();
    void* pRaw = pOwner->Allocate(total);
    if (!pRaw && pOwner != GetDefault())
    {
        pOwner = GetDefault();
        pRaw = pOwner->Allocate(total);
    }
    if (!pRaw)
        return nullptr;

    const auto rawAddress = reinterpret_cast<uintptr_t>(pRaw);
    const auto userAddress = (rawAddress + sizeof(Tag) + aAlignment - 1) & ~(static_cast<uintptr_t>(aAlignment) - 1);

    auto* pTag = reinterpret_cast<Tag*>(userAddress - sizeof(Tag));
    pTag->Owner = pOwner;
    pTag->Raw = pRaw;
    return reinterpret_cast<void*>(userAddress);
}

inline void Allocator::FreeTagged(void* apBlock) noexcept
{
    if (!apBlock)
        return;

    const auto* pTag = reinterpret_cast<const Tag*>(static_cast<unsigned char*>(apBlock) - sizeof(Tag));
    pTag->Owner->Free(pTag->Raw);
}
} // namespace TiltedPhoques
