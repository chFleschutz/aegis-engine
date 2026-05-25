module;
#include <vk_mem_alloc.h>

#include <expected>

module aegis.rhi;
import :memory;
import :context;
import :vulkan_conversions;

namespace aegis::rhi
{
Allocator::Allocator(Allocator&& other) noexcept :
    m_allocator{ std::exchange(other.m_allocator, nullptr) }
{
}

Allocator::~Allocator()
{
    if (m_allocator)
    {
        vmaDestroyAllocator(m_allocator);
    }
}

auto Allocator::operator=(Allocator&& other) noexcept -> Allocator&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
    }
    return *this;
}

auto Allocator::create(
    const vk::raii::Instance& instance,
    const vk::raii::Device& device,
    const vk::raii::PhysicalDevice& physicalDevice)
    -> std::expected<Allocator, Error>
{
    const auto& dispatcher = instance.getDispatcher();

    VmaVulkanFunctions funcs{
        .vkGetInstanceProcAddr = dispatcher->vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = dispatcher->vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo info{
        .physicalDevice = *physicalDevice,
        .device = *device,
        .pVulkanFunctions = &funcs,
        .instance = *instance,
        .vulkanApiVersion = Context::vulkanVersion,
    };

    VmaAllocator allocator{ nullptr };
    auto result = vk::Result{ vmaCreateAllocator(&info, &allocator) };
    if (result != vk::Result::eSuccess)
        return std::unexpected{ makeError(toRHI(result)) };

    return Allocator{ allocator };
}

Allocator::Allocator(VmaAllocator allocator) :
    m_allocator{ allocator }
{
}
}
