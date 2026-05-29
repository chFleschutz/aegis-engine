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
    m_view{ std::exchange(other.m_view, nullptr) },
    m_extent{ other.m_extent },
    m_format{ other.m_format },
    m_mipLevels{ other.m_mipLevels },
    m_arrayLayers{ other.m_arrayLayers }
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
        std::swap(m_extent, other.m_extent);
        std::swap(m_format, other.m_format);
        std::swap(m_mipLevels, other.m_mipLevels);
        std::swap(m_arrayLayers, other.m_arrayLayers);
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

    vk::ImageViewCreateInfo viewInfo{
        .image = vk::Image{ image },
        .viewType = deriveImageViewType(desc.extent, desc.arrayLayers),
        .format = toVulkan(desc.format),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(desc.format),
            .baseMipLevel = 0,
            .levelCount = imageInfo.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = imageInfo.arrayLayers,
        }
    };

    auto view = device.createImageView(viewInfo);
    if (!view.has_value())
        return makeError(toRHI(view.result));

    return Image{
        allocator,
        allocation,
        vk::Image{ image },
        std::move(*view),
        desc.extent,
        desc.format,
        imageInfo.mipLevels,
        imageInfo.arrayLayers,
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
    vk::raii::ImageView view,
    Extent3D extent,
    Format format,
    std::uint32_t mipLevels,
    std::uint32_t arrayLayers) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_image{ image },
    m_view{ std::move(view) },
    m_extent{ extent },
    m_format{ format },
    m_mipLevels{ mipLevels },
    m_arrayLayers{ arrayLayers }
{
}
}
