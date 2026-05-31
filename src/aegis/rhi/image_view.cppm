module;
#include <expected>
#include <string_view>

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

    struct Desc
    {
        std::string_view name;
        Extent3D extent;
        Format format;
        Range range;
    };

    [[nodiscard]] auto operator*() const noexcept -> vk::ImageView { return *m_view; }
    [[nodiscard]] auto handle() const noexcept -> vk::ImageView { return *m_view; }
    [[nodiscard]] auto extent() const noexcept -> Extent3D { return m_extent; }
    [[nodiscard]] auto format() const noexcept -> Format { return m_format; }
    [[nodiscard]] auto range() const noexcept -> Range { return m_range; }
    [[nodiscard]] auto ref() const noexcept -> ImageRef;

private:
    [[nodiscard]] static auto create(const Device& device, vk::Image image, const Desc& desc)
        -> std::expected<ImageView, Error>;

    ImageView(vk::raii::ImageView view, vk::Image image, Extent3D extent, Format format, Range range);

    vk::raii::ImageView m_view;
    vk::Image m_image;
    Extent3D m_extent;
    Format m_format;
    Range m_range;
};
}
