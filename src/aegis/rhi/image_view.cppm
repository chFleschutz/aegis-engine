module;
#include <expected>

export module aegis.rhi:image_view;
import :common;
import :error;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class ImageView
{
    friend Device;
    friend Image;
    friend Swapchain;

public:
    struct Range
    {
        std::uint32_t baseMipLevel{ 0 };
        std::uint32_t mipLevelCount{ 1 };
        std::uint32_t baseArrayLayer{ 0 };
        std::uint32_t arrayLayerCount{ 1 };
    };

    [[nodiscard]] auto extent() const -> Extent3D { return m_extent; }
    [[nodiscard]] auto format() const -> Format { return m_format; }
    [[nodiscard]] auto range() const -> Range { return m_range; }
    [[nodiscard]] auto ref() const noexcept -> ImageRef;

private:
    [[nodiscard]] static auto create(
        const vk::raii::Device& device,
        vk::Image image,
        Extent3D extent,
        Format format,
        const Range& range)
        -> std::expected<ImageView, Error>;

    ImageView(vk::raii::ImageView view, vk::Image image, Extent3D extent, Format format, Range range);

    vk::raii::ImageView m_view;
    vk::Image m_image;
    Extent3D m_extent;
    Format m_format;
    Range m_range;
};
}
