module;
#include <cmath>
#include <expected>
#include <format>

#include "vk_mem_alloc.h"

module aegis.rhi;
import :image;
import :debug;
import :error;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
auto Image::create(const Device& device, const Desc& desc) -> std::expected<Image, Error>
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

    auto imageAlloc = device.allocator().allocateImage(imageInfo, MemoryUsage::GpuOnly);
    if (!imageAlloc)
        return std::unexpected{ imageAlloc.error() };

    debug::setName(*device, imageAlloc->image(), desc.name);

    return Image{
        std::move(*imageAlloc),
        desc,
    };
}

auto Image::calcMipLevels(Extent3D extent) -> std::uint32_t
{
    const auto maxDim = std::max({ extent.x, extent.y, extent.z });
    return static_cast<std::uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

Image::Image(ImageAllocation allocation, const Desc& desc) :
    m_allocation{ std::move(allocation) },
    m_extent{ desc.extent },
    m_format{ desc.format },
    m_usage{ desc.usage },
    m_mipLevels{ desc.mipLevels },
    m_arrayLayers{ desc.arrayLayers }
{
}
}
