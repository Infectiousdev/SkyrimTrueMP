#pragma once

// Deferred start-up work. A namespace-scope `static Initializer name([]{ ... });` registers its
// function during static initialisation, and the application runs them all, once, at a moment of
// its choosing with Initializer::RunAll(). The game uses this to install hooks only after the
// game's own code has loaded.

#include <functional>
#include <utility>
#include <vector>

namespace TiltedPhoques
{
class Initializer
{
public:
    explicit Initializer(std::function<void()> aFunction) { Pending().push_back(std::move(aFunction)); }

    // Runs everything registered so far, in registration order. Each function runs once.
    static void RunAll()
    {
        // Take the list first, so a function that registers another does not invalidate the loop.
        std::vector<std::function<void()>> toRun;
        toRun.swap(Pending());
        for (auto& function : toRun)
            function();
    }

private:
    // A function-local static, so registration is safe during static initialisation in any order.
    static std::vector<std::function<void()>>& Pending()
    {
        static std::vector<std::function<void()>> s_pending;
        return s_pending;
    }
};
} // namespace TiltedPhoques
