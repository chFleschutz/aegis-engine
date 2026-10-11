module;
#include <concepts>
#include <functional>
#include <vector>

export module aegis.rhi:deletion_queue;
import :queue;

export namespace aegis::rhi
{
class DeletionQueue
{
    struct PendingDeletion
    {
        TimelineValue enqueueFrame{};
        std::move_only_function<void()> destroy;
    };

public:
    DeletionQueue() = default;

    /// @brief Keeps 'resource' alive until 'currentFrame' completes; destroying it releases it.
    template<typename T>
    void push(TimelineValue currentFrame, T&& resource)
    {
        m_pending.emplace_back(currentFrame,
            [r = std::forward<T>(resource)]() mutable {
            });
    }

    /// @brief Runs 'fn' once 'currentFrame' completes.
    template<std::invocable F>
    void defer(TimelineValue currentFrame, F&& fn)
    {
        m_pending.emplace_back(currentFrame, std::forward<F>(fn));
    }

    void collect(TimelineValue currentFrame)
    {
        for (auto& entry : m_pending)
        {
            if (entry.enqueueFrame <= currentFrame)
                entry.destroy();
        }

        std::erase_if(m_pending,
            [&](const auto& entry) {
                return entry.enqueueFrame <= currentFrame;
            });
    }

private:
    std::vector<PendingDeletion> m_pending;
};
}
