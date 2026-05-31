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

    auto imageView = ImageView::create(device,
        imageAlloc->image(),
        ImageView::Desc{
            .name = std::format("{}DefaultView", desc.name),
            .extent = desc.extent,
            .format = desc.format,
            .range = ImageView::Range{
                .baseMipLevel = 0,
                .mipLevelCount = imageInfo.mipLevels,
                .baseArrayLayer = 0,
                .arrayLayerCount = imageInfo.arrayLayers,
            }
        });
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
    m_defaultView{ std::move(view) }
{
}
}
