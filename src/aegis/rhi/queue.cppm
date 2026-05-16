module;
#include <cstdint>
#include <expected>
#include <span>

export module aegis.rhi:queue;
import :fwd;
import vulkan_hpp;

namespace aegis::rhi
{
class Queue
{
public:
    Queue(vk::raii::Queue queue, vk::raii::Semaphore semaphore, std::uint32_t queueFamily);

    [[nodiscard]] auto family() const
        -> std::uint32_t { return m_queueFamily; }

private:
    vk::raii::Queue m_queue;
    vk::raii::Semaphore m_semaphore;
    std::uint32_t m_queueFamily;
    std::uint64_t m_submitCounter{ 0 };
};
}
