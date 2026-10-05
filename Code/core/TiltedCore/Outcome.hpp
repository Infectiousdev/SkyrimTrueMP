#pragma once

// A value or an error. `Outcome<ActionEvent, bool>` holds either an ActionEvent or a bool. It
// converts implicitly from either, and is "true" when it holds the value:
//
//   Outcome<Item, bool> Find() { if (none) return false; return item; }
//   if (auto found = Find()) use(found.MoveResult());

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

namespace TiltedPhoques
{
template <class TResult, class TError> class Outcome
{
public:
    static_assert(!std::is_same_v<TResult, TError>, "an Outcome cannot tell its value from its error if they are the same type");

    Outcome(TResult aResult)
        : m_value(std::in_place_index<0>, std::move(aResult))
    {
    }

    Outcome(TError aError)
        : m_value(std::in_place_index<1>, std::move(aError))
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_value.index() == 0; }
    [[nodiscard]] bool HasResult() const noexcept { return m_value.index() == 0; }
    [[nodiscard]] bool HasError() const noexcept { return m_value.index() == 1; }

    [[nodiscard]] TResult& GetResult() noexcept
    {
        assert(HasResult());
        return std::get<0>(m_value);
    }
    [[nodiscard]] const TResult& GetResult() const noexcept
    {
        assert(HasResult());
        return std::get<0>(m_value);
    }

    // Takes the value out, leaving the Outcome holding a moved-from value.
    [[nodiscard]] TResult MoveResult() noexcept
    {
        assert(HasResult());
        return std::move(std::get<0>(m_value));
    }

    [[nodiscard]] const TError& GetError() const noexcept
    {
        assert(HasError());
        return std::get<1>(m_value);
    }

private:
    std::variant<TResult, TError> m_value;
};
} // namespace TiltedPhoques
