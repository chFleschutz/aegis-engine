module;
#include <expected>

module aegis.rhi;
import :queue;

namespace aegis::rhi
{
Queue::Queue(vk::raii::Queue queue, vk::raii::Semaphore semaphore, std::uint32_t queueFamily) :
    m_queue{ std::move(queue) },
    m_semaphore{ std::move(semaphore) },
    m_queueFamily{ queueFamily }
{
}
}
