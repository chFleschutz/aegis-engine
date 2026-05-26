module;
#include <expected>

#include "vk_mem_alloc.h"

module aegis.rhi;
import :image;
import :error;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
Image::Image(Image&& other) noexcept :
    m_allocator{ std::exchange(other.m_allocator, nullptr) },
    m_allocation{ std::exchange(other.m_allocation, nullptr) },
    m_image{ std::exchange(other.m_image, nullptr) },
    m_view{ std::exchange(other.m_view, nullptr) }
{
}

Image::~Image()
{
    if (m_image)
    {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
}

auto Image::operator=(Image&& other) noexcept -> Image&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_allocation, other.m_allocation);
        std::swap(m_image, other.m_image);
        std::swap(m_view, other.m_view);
    }
    return *this;
}

auto Image::create(VmaAllocator allocator, const Desc& desc) -> std::expected<Image, Error>
{
    vk::ImageCreateInfo imageInfo{
        .flags = ,
        .imageType = ,
        .format = ,
        .extent = ,
        .mipLevels = ,
        .arrayLayers = ,
        .samples = ,
        .tiling = ,
        .usage = ,
        .sharingMode = ,
        .queueFamilyIndexCount = ,
        .pQueueFamilyIndices = ,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    VmaAllocationCreateInfo allocationInfo{
        .flags = ,
        .usage = ,
        .requiredFlags = ,
        .preferredFlags = ,
        .memoryTypeBits = ,
        .pool = ,
        .pUserData = ,
        .priority =
    };

    VkImage image;
    VmaAllocation allocation;
    VmaAllocationInfo allocInfo;
    auto result = vk::Result{
        vmaCreateImage(allocator,
            &static_cast<const VkImageCreateInfo&>(imageInfo),
            &allocationInfo,
            &image,
            &allocation,
            &allocInfo)
    };
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return Image{
        allocator,
        allocation,
        vk::Image{ image }
    };
}

Image::Image(VmaAllocator allocator, VmaAllocation allocation, vk::Image image) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_image{ image }
{
}
}
