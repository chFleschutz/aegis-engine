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

auto Allocator::allocateImage(
    const vk::ImageCreateInfo& imageInfo,
    const VmaAllocationCreateInfo& allocCreateInfo) const
    -> std::expected<ImageAllocation, Error>
{
    return ImageAllocation::create(m_allocator, imageInfo, allocCreateInfo);
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

ImageAllocation::ImageAllocation(ImageAllocation&& other) noexcept :
    m_allocator{ std::exchange(other.m_allocator, nullptr) },
    m_allocation{ std::exchange(other.m_allocation, nullptr) },
    m_image{ std::exchange(other.m_image, nullptr) }
{
}

ImageAllocation::~ImageAllocation()
{
    if (m_image)
    {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
}

auto ImageAllocation::operator=(ImageAllocation&& other) noexcept -> ImageAllocation&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_allocation, other.m_allocation);
        std::swap(m_image, other.m_image);
    }
    return *this;
}

auto ImageAllocation::create(VmaAllocator allocator,
    const vk::ImageCreateInfo& imageInfo,
    const VmaAllocationCreateInfo& allocCreateInfo)
    -> std::expected<ImageAllocation, Error>
{
    VkImage image;
    VmaAllocation allocation;
    VmaAllocationInfo allocInfo;
    auto result = vk::Result{
        vmaCreateImage(allocator,
            &static_cast<const VkImageCreateInfo&>(imageInfo),
            &allocCreateInfo,
            &image,
            &allocation,
            &allocInfo)
    };
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return ImageAllocation{ allocator, allocation, vk::Image{ image } };
}

ImageAllocation::ImageAllocation(VmaAllocator allocator, VmaAllocation allocation, vk::Image image) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_image{ image }
{
}
}
