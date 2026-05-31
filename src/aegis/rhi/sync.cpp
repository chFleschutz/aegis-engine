module;
#include <expected>
#include <limits>

module aegis.rhi;
import :sync;
import :device;
import :debug;
import :error;
import :vulkan_conversions;
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

    debug::setName(*device, **fence, desc.name);

    return Fence{ std::move(*fence) };
}

Fence::Fence(vk::raii::Fence fence) :
    m_fence{ std::move(fence) }
{
}

auto Semaphore::create(const vk::raii::Device& device, const Desc& desc) -> std::expected<Semaphore, Error>
{
    auto semaphoreTypeInfo = vk::SemaphoreTypeCreateInfo{
        .semaphoreType = vk::SemaphoreType::eTimeline,
    };

    auto semaphoreInfo = vk::SemaphoreCreateInfo{
        .pNext = desc.type == Type::Timeline ? &semaphoreTypeInfo : nullptr,
    };

    auto semaphore = device.createSemaphore(semaphoreInfo);
    if (!semaphore.has_value())
        return makeError(toRHI(semaphore.result));

    debug::setName(device, **semaphore, desc.name);

    return Semaphore{ std::move(*semaphore) };
}

Semaphore::Semaphore(vk::raii::Semaphore semaphore) :
    m_semaphore{ std::move(semaphore) }
{
}
}
