module;
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

    template<typename T>
    void push(TimelineValue currentFrame, T&& resource)
    {
        m_pending.emplace_back(currentFrame,
            [r = std::forward<T>(resource)]() mutable {
            });
    }

    void collect(TimelineValue currentFrame)
    {
        std::erase_if(m_pending,
            [&](const auto& entry) {
                return entry.enqueueFrame <= currentFrame;
            });
    }

private:
    std::vector<PendingDeletion> m_pending;
};
}
