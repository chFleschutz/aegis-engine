module;
#include <expected>

module aegis.rhi;
import :image_view;
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

auto ImageView::create(
    const vk::raii::Device& device,
    vk::Image image,
    Extent3D extent,
    Format format,
    const Range& range)
    -> std::expected<ImageView, Error>
{
    vk::ImageViewCreateInfo viewInfo{
        .image = image,
        .viewType = deriveImageViewType(extent, range.arrayLayerCount),
        .format = toVulkan(format),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(format),
            .baseMipLevel = range.baseMipLevel,
            .levelCount = range.mipLevelCount,
            .baseArrayLayer = range.baseArrayLayer,
            .layerCount = range.arrayLayerCount,
        }
    };

    auto view = device.createImageView(viewInfo);
    if (!view.has_value())
        return makeError(toRHI(view.result));

    return ImageView{
        std::move(*view),
        image,
        extent,
        format,
        range
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
