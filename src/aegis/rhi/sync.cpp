module;
#include <expected>
#include <limits>

module aegis.rhi;
import :sync;
import :device;

namespace aegis::rhi
{
auto Fence::create(const Desc& desc)
    -> std::expected<Fence, Error>
{
    vk::FenceCreateInfo fenceInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    auto fence = desc.device->createFence(fenceInfo);
    if (!fence.has_value())
        return vkError(fence.result, "Failed to create fence");

    return Fence{ std::move(*fence) };
}

auto Fence::wait() const noexcept
    -> bool
{
    auto result = m_fence.getDevice().waitForFences(*m_fence,
        vk::True,
        std::numeric_limits<std::uint64_t>::max(),
        *m_fence.getDispatcher()
    );
    return result == vk::Result::eSuccess;
}

auto Fence::reset() const noexcept
    -> bool
{
    auto result = m_fence.getDevice().resetFences(*m_fence, *m_fence.getDispatcher());
    return result == vk::Result::eSuccess;
}

Fence::Fence(vk::raii::Fence fence) :
    m_fence{ std::move(fence) }
{
}

auto Semaphore::create(const Desc& desc)
    -> std::expected<Semaphore, Error>
{
    vk::SemaphoreCreateInfo semaphoreInfo{};
    auto semaphore = desc.device->createSemaphore(semaphoreInfo);
    if (!semaphore.has_value())
        return vkError(semaphore.result, "Failed to create semaphore");

    return Semaphore{ std::move(*semaphore) };
}

Semaphore::Semaphore(vk::raii::Semaphore semaphore) :
    m_semaphore{ std::move(semaphore) }
{
}
}
