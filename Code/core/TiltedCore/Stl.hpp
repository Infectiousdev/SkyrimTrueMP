#pragma once

// Standard containers wired to the current allocator (see Allocator.hpp), plus the smart-pointer
// helpers the game code uses.

#include "Allocator.hpp"
#include "Meta.hpp"

#include <map>
#include <memory>
#include <string>
#include <tsl/hopscotch_map.h>
#include <tsl/hopscotch_set.h>
#include <utility>
#include <vector>

namespace TiltedPhoques
{
template <class T> using Vector = std::vector<T, StlAllocator<T>>;

// Hash tables (hopscotch): fast lookups, and iteration order is unspecified.
template <class T, class U> using Map = tsl::hopscotch_map<T, U, std::hash<T>, std::equal_to<T>, StlAllocator<std::pair<T, U>>>;
template <class T> using Set = tsl::hopscotch_set<T, std::hash<T>, std::equal_to<T>, StlAllocator<T>>;

// An ordered map, for when iteration order matters.
template <class T, class U> using SortedMap = std::map<T, U, std::less<T>, StlAllocator<std::pair<const T, U>>>;

using String = std::basic_string<char, std::char_traits<char>, StlAllocator<char>>;
using WString = std::basic_string<wchar_t, std::char_traits<wchar_t>, StlAllocator<wchar_t>>;

template <class T> using UniquePtr = std::unique_ptr<T>;
template <class T> using SharedPtr = std::shared_ptr<T>;

template <class T, class... TArgs> [[nodiscard]] UniquePtr<T> MakeUnique(TArgs&&... aArgs)
{
    return std::make_unique<T>(std::forward<TArgs>(aArgs)...);
}

template <class T, class... TArgs> [[nodiscard]] SharedPtr<T> MakeShared(TArgs&&... aArgs)
{
    return std::make_shared<T>(std::forward<TArgs>(aArgs)...);
}

// Hands ownership from a pointer to a base class to a pointer to a derived class. The caller must
// know the object really is a TTo.
template <class TTo, class TFrom> [[nodiscard]] UniquePtr<TTo> CastUnique(UniquePtr<TFrom>&& aPointer) noexcept
{
    return UniquePtr<TTo>(static_cast<TTo*>(aPointer.release()));
}
} // namespace TiltedPhoques
