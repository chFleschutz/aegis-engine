module;
#include <algorithm>
#include <bit>
#include <cmath>
#include <expected>
#include <format>
#include <utility>
#include <vector>

module aegis.rhi;
import :image;
import :debug;
import :error;
import :utility;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi::detail
{
auto calcMipLevels(Extent3D extent) -> std::uint32_t
{
    const auto maxDim = std::max({ extent.x, extent.y, extent.z });
    return static_cast<std::uint32_t>(std::floor(std::log2(maxDim))) + 1;
}

constexpr auto bytesPerTexel(Format format) noexcept -> std::uint32_t
{
    switch (format)
    {
    case Format::Unknown:
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
        return 0;
    case Format::R8_UNORM:
        return 1;
    case Format::RG8_UNORM:
    case Format::R16_UNORM:
        return 2;
    case Format::RGBA8_UNORM:
    case Format::RGBA8_SRGB:
    case Format::BGRA8_UNORM:
    case Format::BGRA8_SRGB:
    case Format::RGB10A2_UNORM:
    case Format::B10G11R11_UFLOAT:
    case Format::RG16_UNORM:
    case Format::RGBA16_UNORM:
    case Format::R32_SFLOAT:
    case Format::D32_SFLOAT:
        return 4;
    case Format::RGBA16_SFLOAT:
    case Format::RG32_SFLOAT:
        return 8;
    case Format::RGB32_SFLOAT:
        return 12;
    case Format::RGBA32_SFLOAT:
        return 16;
    }
    std::unreachable();
}

constexpr auto mipExtent(Extent3D base, std::uint32_t level) noexcept -> Extent3D
{
    // A shift wider than the operand (32 bit) is UB, and every axis has bottomed out at 1 long before 31.
    const auto shift = std::min(level, std::uint32_t{ 31 });
    return Extent3D{
        std::max(std::uint32_t{ 1 }, base.x >> shift),
        std::max(std::uint32_t{ 1 }, base.y >> shift),
        std::max(std::uint32_t{ 1 }, base.z >> shift),
    };
}

constexpr auto copyAlignment(Format format) noexcept -> std::size_t
{
    const auto texelSize = bytesPerTexel(format);
    if (texelSize == 0)
        return 4;
    return std::max<std::size_t>(4, std::bit_ceil(std::size_t{ texelSize }));
}

auto subresourceFootprints(Extent3D extent, Format format, std::uint32_t mipLevels,
    std::uint32_t arrayLayers) -> std::vector<SubresourceFootprint>
{
    const auto texelSize = bytesPerTexel(format);
    if (texelSize == 0 || mipLevels == 0 || arrayLayers == 0)
        return {};

    const auto alignment = copyAlignment(format);

    std::vector<SubresourceFootprint> footprints;
    footprints.reserve(mipLevels);

    std::size_t sourceOffset = 0;
    std::size_t stagingOffset = 0;
    for (std::uint32_t level = 0; level < mipLevels; ++level)
    {
        const auto levelExtent = mipExtent(extent, level);
        const auto size = std::size_t{ texelSize } * levelExtent.x * levelExtent.y * levelExtent.z *
                          arrayLayers;

        stagingOffset = utility::alignTo(stagingOffset, alignment);
        footprints.emplace_back(SubresourceFootprint{
            .mipLevel = level,
            .arrayLayerCount = arrayLayers,
            .extent = levelExtent,
            .sourceOffset = sourceOffset,
            .stagingOffset = stagingOffset,
            .size = size,
        });

        sourceOffset += size;
        stagingOffset += size;
    }
    return footprints;
}
}

namespace aegis::rhi
{
auto Image::create(const Device& device, const Desc& desc) -> std::expected<Image, Error>
{
    std::uint32_t mipLevels = desc.mipLevels == Image::fullMipChain
                                  ? detail::calcMipLevels(desc.extent)
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
        mipLevels,
    };
}

Image::Image(ImageAllocation allocation, const Desc& desc, std::uint32_t mipLevels) :
    m_allocation{ std::move(allocation) },
    m_extent{ desc.extent },
    m_format{ desc.format },
    m_usage{ desc.usage },
    m_mipLevels{ mipLevels },
    m_arrayLayers{ desc.arrayLayers }
{
}
}
