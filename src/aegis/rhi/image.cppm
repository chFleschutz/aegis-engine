module;
#include <expected>
#include <limits>
#include <string_view>

export module aegis.rhi:image;
import :common;
import :error;
import :fwd;
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
        std::string_view name;
        Extent3D extent;
        Format format;
        ImageUsage usage;
        std::uint32_t mipLevels{ 1 };
        std::uint32_t arrayLayers{ 1 };
    };

    static constexpr std::uint32_t fullMipChain{ std::numeric_limits<std::uint32_t>::max() };

    [[nodiscard]] auto vk() const noexcept -> vk::Image { return m_allocation.image(); }
    [[nodiscard]] auto extent() const noexcept -> Extent3D { return m_extent; }
    [[nodiscard]] auto format() const noexcept -> Format { return m_format; }
    [[nodiscard]] auto usage() const noexcept -> ImageUsage { return m_usage; }
    [[nodiscard]] auto mipLevels() const noexcept -> std::uint32_t { return m_mipLevels; }
    [[nodiscard]] auto arrayLayers() const noexcept -> std::uint32_t { return m_arrayLayers; }

private:
    [[nodiscard]] static auto create(const Device& device, const Desc& desc)
        -> std::expected<Image, Error>;

    [[nodiscard]] static auto calcMipLevels(Extent3D extent) -> std::uint32_t;

    Image(ImageAllocation allocation, const Desc& desc);

    ImageAllocation m_allocation;
    Extent3D m_extent;
    Format m_format;
    ImageUsage m_usage;
    std::uint32_t m_mipLevels;
    std::uint32_t m_arrayLayers;
};
}
