module;
#include <expected>

module aegis.rhi;
import :image_view;
import :debug;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
auto ImageView::ref() const noexcept -> ImageRef
{
    return ImageRef{
        .image = m_image,
        .view = *m_view,
        .extent = m_extent,
        .format = m_format,
        .baseMipLevel = m_range.baseMipLevel,
        .levelCount = m_range.mipLevelCount,
        .baseArrayLayer = m_range.baseArrayLayer,
        .layerCount = m_range.arrayLayerCount,
    };
}

auto ImageView::create(const Device& device, vk::Image image, const Desc& desc)
    -> std::expected<ImageView, Error>
{
    vk::ImageViewCreateInfo viewInfo{
        .image = image,
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
        image,
        desc.extent,
        desc.format,
        desc.range
    };
}

ImageView::ImageView(
    vk::raii::ImageView view,
    vk::Image image,
    Extent3D extent,
    Format format,
    Range range) :
    m_view{ std::move(view) },
    m_image{ image },
    m_extent{ extent },
    m_format{ format },
    m_range{ range }
{
}
}
