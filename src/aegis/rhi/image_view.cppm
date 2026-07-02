module;
#include <expected>
#include <optional>
#include <string_view>

export module aegis.rhi:image_view;
import :common;
import :error;
import :fwd;
import :resource_pool;
import :bindless_heap;
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
        ImageUsage usage;
    };

    [[nodiscard]] auto vk() const noexcept -> vk::ImageView { return *m_view; }
    [[nodiscard]] auto vkImage() const noexcept -> vk::Image { return m_imageSrc; }
    [[nodiscard]] auto extent() const noexcept -> Extent3D { return m_extent; }
    [[nodiscard]] auto format() const noexcept -> Format { return m_format; }
    [[nodiscard]] auto range() const noexcept -> Range { return m_range; }
    [[nodiscard]] auto sampledHandle() const noexcept -> std::optional<SampledImageHandle>;
    [[nodiscard]] auto storageHandle() const noexcept -> std::optional<StorageImageHandle>;

    auto setSampledHandle(SampledImageHandle handle) noexcept -> void { m_sampledHandle = handle; }
    auto setStorageHandle(StorageImageHandle handle) noexcept -> void { m_storageHandle = handle; }

private:
    [[nodiscard]] static auto create(const Device& device, ImageHandle image, const Desc& desc)
        -> std::expected<ImageView, Error>;

    [[nodiscard]] static auto create(const Device& device, vk::Image imageSrc, const Desc& desc)
        -> std::expected<ImageView, Error>;

    ImageView(vk::raii::ImageView view, vk::Image imageSrc, std::optional<ImageHandle> imageHandle,
        Extent3D extent, Format format, Range range);

    vk::raii::ImageView m_view;
    vk::Image m_imageSrc;
    std::optional<ImageHandle> m_image;
    Extent3D m_extent;
    Format m_format;
    Range m_range;
    std::optional<SampledImageHandle> m_sampledHandle;
    std::optional<StorageImageHandle> m_storageHandle;
};
}
