module;
#include <cstdint>
#include <expected>

export module aegis.rhi:queue;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class Queue
{
public:
    struct SubmitInfo
    {
        const CommandBuffer& commandBuffer;
        const Semaphore& waitSemaphore;
        const Semaphore& signalSemaphore;
    };

    Queue(vk::raii::Queue queue, vk::raii::Semaphore semaphore, std::uint32_t queueFamily);

    [[nodiscard]] auto operator*() const -> vk::Queue { return *m_queue; }
    [[nodiscard]] auto operator->() const -> const vk::raii::Queue* { return &m_queue; }
    [[nodiscard]] auto family() const -> std::uint32_t { return m_queueFamily; }

    [[nodiscard]] auto submit(const SubmitInfo& info) -> std::expected<std::uint64_t, Error>;

    auto wait(std::uint64_t timePoint) const -> bool;
    auto waitIdle() const -> bool;

private:
    vk::raii::Queue m_queue;
    vk::raii::Semaphore m_timeline;
    std::uint32_t m_queueFamily;
    std::uint64_t m_submitCounter{ 0 };
};
}
