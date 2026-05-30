module;
#include <expected>

export module aegis.rhi:image_view;
import :common;
import :error;
import :image;
import vulkan_hpp;

export namespace aegis::rhi
{
class ImageView
{
    friend Device;

public:
    struct Desc
    {
        std::uint32_t baseMipLevel{ 0 };
        std::uint32_t mipLevelCount{ 1 };
        std::uint32_t baseArrayLayer{ 0 };
        std::uint32_t arrayLayerCount{ 1 };
    };

    [[nodiscard]] auto extent() const -> Extent3D { return m_extent; }
    [[nodiscard]] auto format() const -> Format { return m_format; }
    [[nodiscard]] auto baseMipLevel() const -> std::uint32_t { return m_baseMipLevel; }
    [[nodiscard]] auto mipLevelCount() const -> std::uint32_t { return m_mipLevelCount; }
    [[nodiscard]] auto baseArrayLayer() const -> std::uint32_t { return m_baseArrayLayer; }
    [[nodiscard]] auto arrayLayerCount() const -> std::uint32_t { return m_arrayLayerCount; }
    [[nodiscard]] auto ref() const noexcept -> ImageRef;

private:
    [[nodiscard]] static auto create(
        const vk::raii::Device& device,
        const Image& image,
        const Desc& desc)
        -> std::expected<ImageView, Error>;

    ImageView(vk::raii::ImageView view, vk::Image image, Extent3D extent, Format format, const Desc& desc);

    vk::raii::ImageView m_view;
    vk::Image m_image;
    Extent3D m_extent;
    Format m_format;
    std::uint32_t m_baseMipLevel;
    std::uint32_t m_mipLevelCount;
    std::uint32_t m_baseArrayLayer;
    std::uint32_t m_arrayLayerCount;
};
}
