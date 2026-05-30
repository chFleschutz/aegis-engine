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
auto Image::create(
    const vk::raii::Device& device,
    const Allocator& allocator,
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

    auto imageAlloc = allocator.allocateImage(imageInfo, allocationInfo);
    if (!imageAlloc)
        return std::unexpected{ imageAlloc.error() };

    ImageView::Range imageViewDesc{
        .baseMipLevel = 0,
        .mipLevelCount = imageInfo.mipLevels,
        .baseArrayLayer = 0,
        .arrayLayerCount = imageInfo.arrayLayers,
    };

    auto imageView = ImageView::create(device, imageAlloc->image(), desc.extent, desc.format, imageViewDesc);
    if (!imageView)
        return std::unexpected{ imageView.error() };

    return Image{
        std::move(*imageAlloc),
        std::move(*imageView),
    };
}

auto Image::calcMipLevels(Extent3D extent) -> std::uint32_t
{
    const auto maxDim = std::max({ extent.x, extent.y, extent.z });
    return static_cast<std::uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

Image::Image(ImageAllocation allocation, ImageView view) :
    m_allocation{ std::move(allocation) },
    m_fullView{ std::move(view) }
{
}
}
