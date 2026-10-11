module;
#include <expected>
#include <optional>
#include <string_view>
#include <utility>

export module aegis.rhi:image_view;
import :common;
import :error;
import :fwd;
import :resource_pool;
import :bindless_heap;
import vulkan_hpp;

export namespace aegis::rhi::detail
{
/// @brief The view type a view spanning 'layerCount' layers of an image of 'imageType' gets when
///        ImageView::Desc::type is not set: the image's own type, as an array over several layers.
///        A cube image viewed through fewer than 6 layers (e.g. one face as a render target) is 2D.
[[nodiscard]] constexpr auto defaultViewType(ImageType imageType, std::uint32_t layerCount) noexcept
    -> ImageViewType
{
    switch (imageType)
    {
    case ImageType::Dim1D:
        return layerCount > 1 ? ImageViewType::Dim1DArray : ImageViewType::Dim1D;
    case ImageType::Dim2D:
        return layerCount > 1 ? ImageViewType::Dim2DArray : ImageViewType::Dim2D;
    case ImageType::Dim3D:
        return ImageViewType::Dim3D;
    case ImageType::Cube:
        if (layerCount == 6)
            return ImageViewType::Cube;
        if (layerCount % 6 == 0)
            return ImageViewType::CubeArray;
        return layerCount > 1 ? ImageViewType::Dim2DArray : ImageViewType::Dim2D;
    }
    std::unreachable();
}
}

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
        std::optional<ImageViewType> type; // Defaults to detail::defaultViewType
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
    [[nodiscard]] auto sampledDescriptor() const noexcept -> std::optional<SampledImageDescriptor>;
    [[nodiscard]] auto storageDescriptor() const noexcept -> std::optional<StorageImageDescriptor>;

    auto setSampledDescriptor(SampledImageDescriptor descriptor) noexcept -> void { m_sampledDescriptor = descriptor; }
    auto setStorageDescriptor(StorageImageDescriptor descriptor) noexcept -> void { m_storageDescriptor = descriptor; }

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
    std::optional<SampledImageDescriptor> m_sampledDescriptor;
    std::optional<StorageImageDescriptor> m_storageDescriptor;
};
}
