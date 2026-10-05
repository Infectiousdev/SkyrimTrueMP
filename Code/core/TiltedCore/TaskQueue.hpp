#pragma once

// Work handed from any thread to one thread. Producers Add() functions from wherever they run; the
// owning thread (the game's main thread) calls Drain() once per frame to run what has arrived.

#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace TiltedPhoques
{
class TaskQueue
{
public:
    using Task = std::function<void()>;

    // Safe to call from any thread.
    void Add(Task aTask)
    {
        std::lock_guard lock(m_mutex);
        m_tasks.push_back(std::move(aTask));
    }

    // Runs every task queued at the moment of the call, oldest first. A task that queues more work
    // does not extend this call, so one frame cannot be starved by tasks that keep requeueing.
    void Drain()
    {
        std::deque<Task> batch;
        {
            std::lock_guard lock(m_mutex);
            batch.swap(m_tasks);
        }

        for (auto& task : batch)
            task();
    }

    [[nodiscard]] size_t Size() const
    {
        std::lock_guard lock(m_mutex);
        return m_tasks.size();
    }

private:
    mutable std::mutex m_mutex;
    std::deque<Task> m_tasks;
};
} // namespace TiltedPhoques
