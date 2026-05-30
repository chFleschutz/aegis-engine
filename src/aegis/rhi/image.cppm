module;
#include <expected>
#include <limits>

export module aegis.rhi:image;
import :common;
import :error;
import :fwd;
import :image_view;
import :image_ref;
import :memory;
import vulkan_hpp;

export namespace aegis::rhi
{
class Image
{
    friend Device;

public:
    struct Desc
    {
        Extent3D extent;
        Format format;
        ImageUsage usage;
        std::uint32_t mipLevels{ 1 };
        std::uint32_t arrayLayers{ 1 };
        MemoryType memoryType{ MemoryType::GPUOnly };
    };

    static constexpr std::uint32_t fullMipChain{ std::numeric_limits<std::uint32_t>::max() };

    [[nodiscard]] auto image() const noexcept -> vk::Image { return m_allocation.image(); }
    [[nodiscard]] auto extent() const noexcept -> Extent3D { return m_fullView.extent(); }
    [[nodiscard]] auto format() const noexcept -> Format { return m_fullView.format(); }
    [[nodiscard]] auto ref() const noexcept -> ImageRef { return m_fullView.ref(); }

private:
    [[nodiscard]] static auto create(
        const vk::raii::Device& device,
        const Allocator& allocator,
        const Desc& desc)
        -> std::expected<Image, Error>;

    [[nodiscard]] static auto calcMipLevels(Extent3D extent) -> std::uint32_t;

    Image(ImageAllocation allocation, ImageView view);

    ImageAllocation m_allocation;
    ImageView m_fullView;
};
}
