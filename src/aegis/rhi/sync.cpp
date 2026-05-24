module;
#include <expected>
#include <limits>

module aegis.rhi;
import :sync;
import :device;
import :error;
import :vulkan;
import vulkan_hpp;

namespace aegis::rhi
{
auto Fence::wait() const noexcept -> bool
{
    auto result = m_fence.getDevice().waitForFences(*m_fence,
        vk::True,
        std::numeric_limits<std::uint64_t>::max(),
        *m_fence.getDispatcher()
    );
    return result == vk::Result::eSuccess;
}

auto Fence::reset() const noexcept -> bool
{
    auto result = m_fence.getDevice().resetFences(*m_fence, *m_fence.getDispatcher());
    return result == vk::Result::eSuccess;
}

auto Fence::create(const Device& device, const Desc& desc) -> std::expected<Fence, Error>
{
    vk::FenceCreateInfo fenceInfo{
        .flags = desc.signaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlagBits{},
    };
    auto fence = device->createFence(fenceInfo);
    if (!fence.has_value())
        return makeError(toRHI(fence.result));

    return Fence{ std::move(*fence) };
}

Fence::Fence(vk::raii::Fence fence) :
    m_fence{ std::move(fence) }
{
}

auto Semaphore::create(const Device& device, const Desc& desc) -> std::expected<Semaphore, Error>
{
    vk::SemaphoreCreateInfo semaphoreInfo{};
    auto semaphore = device->createSemaphore(semaphoreInfo);
    if (!semaphore.has_value())
        return makeError(toRHI(semaphore.result));

    return Semaphore{ std::move(*semaphore) };
}

Semaphore::Semaphore(vk::raii::Semaphore semaphore) :
    m_semaphore{ std::move(semaphore) }
{
}
}
