module;
#include "vma/vma.h"

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

Allocation::Allocation(Allocation&& other) noexcept :
    m_allocator{ other.m_allocator },
    m_allocation{ std::exchange(other.m_allocation, nullptr) }
{
}

Allocation::~Allocation()
{
    if (m_allocation)
    {
        vmaFreeMemory(m_allocator, m_allocation);
    }
}

auto Allocation::operator=(Allocation&& other) noexcept -> Allocation&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_allocation, other.m_allocation);
    }
    return *this;
}

auto Allocation::map() const -> std::expected<void*, Error>
{
    void* ptr{ nullptr };
    auto result = vk::Result{ vmaMapMemory(m_allocator, m_allocation, &ptr) };
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));
    return ptr;
}

auto Allocation::unmap() const -> void
{
    vmaUnmapMemory(m_allocator, m_allocation);
}

auto Allocation::create(const Desc& desc) -> std::expected<Allocation, Error>
{
    // TODO: implement
    return std::unexpected{ makeError(ErrorCode::Unknown) };
}

Allocation::Allocation(VmaAllocator allocator, VmaAllocation allocation) :
    m_allocator{ allocator },
    m_allocation{ allocation }
{
}
}
