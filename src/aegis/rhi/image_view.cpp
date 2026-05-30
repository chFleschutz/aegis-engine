module;

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
        .baseMipLevel = m_baseMipLevel,
        .levelCount = m_mipLevelCount,
        .baseArrayLayer = m_baseArrayLayer,
        .layerCount = m_arrayLayerCount,
    };
}

auto ImageView::create(
    const vk::raii::Device& device,
    const Image& image,
    const Desc& desc)
    -> std::expected<ImageView, Error>
{
    vk::ImageViewCreateInfo viewInfo{
        .image = image.image(),
        .viewType = deriveImageViewType(image.extent(), desc.arrayLayerCount),
        .format = toVulkan(image.format()),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(image.format()),
            .baseMipLevel = desc.baseMipLevel,
            .levelCount = desc.mipLevelCount,
            .baseArrayLayer = desc.baseArrayLayer,
            .layerCount = desc.arrayLayerCount,
        }
    };

    auto view = device.createImageView(viewInfo);
    if (!view.has_value())
        return makeError(toRHI(view.result));

    return ImageView{
        std::move(*view),
        image.image(),
        image.extent(),
        image.format(),
        desc
    };
}

ImageView::ImageView(vk::raii::ImageView view,
    vk::Image image,
    Extent3D extent,
    Format format,
    const Desc& desc) :
    m_view{ std::move(view) },
    m_image{ image },
    m_extent{ extent },
    m_format{ format },
    m_baseMipLevel{ desc.baseMipLevel },
    m_mipLevelCount{ desc.mipLevelCount },
    m_baseArrayLayer{ desc.baseArrayLayer },
    m_arrayLayerCount{ desc.arrayLayerCount }
{
}
}
