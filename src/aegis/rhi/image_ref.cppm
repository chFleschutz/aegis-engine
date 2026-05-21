module;
#include <expected>

export module aegis.rhi:image_ref;
import :error;
import vulkan_hpp;

namespace aegis::rhi
{
class ImageRef
{
public:
    ImageRef(vk::Image image,
        vk::ImageView view,
        Format format,
        Extent3D extent,
        std::uint32_t baseMipLevel = 0,
        std::uint32_t levelCount = 1,
        std::uint32_t baseArrayLayer = 0,
        std::uint32_t layerCount = 1);

    [[nodiscard]] auto image() const noexcept -> vk::Image { return m_image; }
    [[nodiscard]] auto view() const noexcept -> vk::ImageView { return m_view; }
    [[nodiscard]] auto format() const noexcept -> Format { return m_format; }
    [[nodiscard]] auto extent() const noexcept -> Extent3D { return m_extent; }
    [[nodiscard]] auto baseMipLevel() const noexcept -> std::uint32_t { return m_baseMipLevel; }
    [[nodiscard]] auto levelCount() const noexcept -> std::uint32_t { return m_levelCount; }
    [[nodiscard]] auto baseArrayLayer() const noexcept -> std::uint32_t { return m_baseArrayLayer; }
    [[nodiscard]] auto layerCount() const noexcept -> std::uint32_t { return m_layerCount; }

private:
    vk::Image m_image;
    vk::ImageView m_view;
    Format m_format;
    Extent3D m_extent;
    std::uint32_t m_baseMipLevel;
    std::uint32_t m_levelCount;
    std::uint32_t m_baseArrayLayer;
    std::uint32_t m_layerCount;
};
}
