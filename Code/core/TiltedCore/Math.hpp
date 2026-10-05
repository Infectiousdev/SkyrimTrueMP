#pragma once

#include <cmath>
#include <type_traits>

namespace TiltedPhoques
{
inline constexpr double Pi = 3.14159265358979323846;

// The remainder of aValue / aDivisor, with the sign of aValue (like std::fmod). Mixed float and double
// arguments are fine; the result has the wider type.
template <class T, class U> [[nodiscard]] inline auto Mod(T aValue, U aDivisor) noexcept -> std::enable_if_t<std::is_arithmetic_v<T> && std::is_arithmetic_v<U>, std::common_type_t<T, U, float>>
{
    using R = std::common_type_t<T, U, float>;
    return std::fmod(static_cast<R>(aValue), static_cast<R>(aDivisor));
}

// Linear interpolation: aFrom at aT = 0, aTo at aT = 1. Works for any type with +, - and scaling by a
// float, so vectors interpolate as well as numbers.
template <class T, class TFactor> [[nodiscard]] inline auto Lerp(const T& aFrom, const T& aTo, TFactor aT) noexcept(noexcept(aFrom + (aTo - aFrom) * aT))
{
    return aFrom + (aTo - aFrom) * aT;
}

// The shortest signed turn from aFrom to aTo, in (-half a turn, +half a turn]. Radians when
// aRadians is true, degrees otherwise.
template <class T> [[nodiscard]] inline std::enable_if_t<std::is_floating_point_v<T>, T> DeltaAngle(T aFrom, T aTo, bool aRadians) noexcept
{
    const T turn = aRadians ? static_cast<T>(2 * Pi) : T(360);
    T delta = std::fmod(aTo - aFrom, turn);

    // Bring into (-turn/2, turn/2].
    if (delta > turn / 2)
        delta -= turn;
    else if (delta <= -turn / 2)
        delta += turn;
    return delta;
}
} // namespace TiltedPhoques
