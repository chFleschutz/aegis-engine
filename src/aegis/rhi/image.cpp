module;
#include <cmath>
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
    m_fullView{ std::move(other.m_fullView) }
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
        std::swap(m_fullView, other.m_fullView);
    }
    return *this;
}

auto Image::create(const vk::raii::Device& device,
    VmaAllocator allocator,
    const Desc& desc) -> std::expected<Image, Error>
{
    std::uint32_t mipLevels = desc.mipLevels == Image::fullMipChain
                                  ? calcMipLevels(desc.extent)
                                  : desc.mipLevels;

    vk::ImageCreateInfo imageInfo{
        .flags = deriveImageCreateFlags(desc.extent, desc.arrayLayers),
        .imageType = deriveImageType(desc.extent),
        .format = toVulkan(desc.format),
        .extent = toVulkan(desc.extent),
        .mipLevels = mipLevels,
        .arrayLayers = desc.arrayLayers,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = toVulkan(desc.usage),
        .sharingMode = vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = {},
        .pQueueFamilyIndices = nullptr,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    VmaAllocationCreateInfo allocationInfo{ deriveAllocationInfo(desc.memoryType) };

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

    ImageView::Range imageViewDesc{
        .baseMipLevel = 0,
        .mipLevelCount = imageInfo.mipLevels,
        .baseArrayLayer = 0,
        .arrayLayerCount = imageInfo.arrayLayers,
    };

    auto imageView = ImageView::create(device, vk::Image{ image }, desc.extent, desc.format, imageViewDesc);
    if (!imageView)
        return std::unexpected{ imageView.error() };

    return Image{
        allocator,
        allocation,
        vk::Image{ image },
        std::move(*imageView),
    };
}

auto Image::calcMipLevels(Extent3D extent) -> std::uint32_t
{
    const auto maxDim = std::max({ extent.x, extent.y, extent.z });
    return static_cast<std::uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

Image::Image(
    VmaAllocator allocator,
    VmaAllocation allocation,
    vk::Image image,
    ImageView view) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_image{ image },
    m_fullView{ std::move(view) }
{
}
}
