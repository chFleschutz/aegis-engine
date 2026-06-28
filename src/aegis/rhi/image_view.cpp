module;
#include <expected>
#include <optional>

module aegis.rhi;
import :image_view;
import :debug;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
auto ImageView::create(const Device& device, ImageHandle image, const Desc& desc)
    -> std::expected<ImageView, Error>
{
    auto imageSrc = device.get(image).vk();
    vk::ImageViewCreateInfo viewInfo{
        .image = imageSrc,
        .viewType = deriveImageViewType(desc.extent, desc.range.arrayLayerCount),
        .format = toVulkan(desc.format),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(desc.format),
            .baseMipLevel = desc.range.baseMipLevel,
            .levelCount = desc.range.mipLevelCount,
            .baseArrayLayer = desc.range.baseArrayLayer,
            .layerCount = desc.range.arrayLayerCount,
        }
    };

    auto view = device->createImageView(viewInfo);
    if (!view.has_value())
        return makeError(toRHI(view.result));

    debug::setName(*device, **view, desc.name);

    return ImageView{
        std::move(*view),
        imageSrc,
        image,
        desc.extent,
        desc.format,
        desc.range
    };
}

auto ImageView::create(const Device& device, vk::Image imageSrc,
    const Desc& desc) -> std::expected<ImageView, Error>
{
    vk::ImageViewCreateInfo viewInfo{
        .image = imageSrc,
        .viewType = deriveImageViewType(desc.extent, desc.range.arrayLayerCount),
        .format = toVulkan(desc.format),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(desc.format),
            .baseMipLevel = desc.range.baseMipLevel,
            .levelCount = desc.range.mipLevelCount,
            .baseArrayLayer = desc.range.baseArrayLayer,
            .layerCount = desc.range.arrayLayerCount,
        }
    };

    auto view = device->createImageView(viewInfo);
    if (!view.has_value())
        return makeError(toRHI(view.result));

    debug::setName(*device, **view, desc.name);

    return ImageView{
        std::move(*view),
        imageSrc,
        std::nullopt,
        desc.extent,
        desc.format,
        desc.range
    };
}

ImageView::ImageView(vk::raii::ImageView view, vk::Image imageSrc, std::optional<ImageHandle> imageHandle,
    Extent3D extent, Format format, Range range) :
    m_view{ std::move(view) },
    m_imageSrc{ imageSrc },
    m_image{ imageHandle },
    m_extent{ extent },
    m_format{ format },
    m_range{ range }
{
}
}
