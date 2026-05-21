module;
#include <cstdint>

module aegis.rhi;
import :image_ref;

namespace aegis::rhi
{
ImageRef::ImageRef(vk::Image image,
    vk::ImageView view,
    Format format,
    Extent3D extent,
    std::uint32_t baseMipLevel,
    std::uint32_t levelCount,
    std::uint32_t baseArrayLayer,
    std::uint32_t layerCount) :
    m_image{ image },
    m_view{ view },
    m_format{ format },
    m_extent{ extent },
    m_baseMipLevel{ baseMipLevel },
    m_levelCount{ levelCount },
    m_baseArrayLayer{ baseArrayLayer },
    m_layerCount{ layerCount }
{
}
}
