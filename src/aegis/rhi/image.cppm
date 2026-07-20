module;
#include <algorithm>
#include <expected>
#include <limits>
#include <string_view>
#include <vector>

export module aegis.rhi:image;
import :common;
import :error;
import :fwd;
import :memory;
import vulkan_hpp;

export namespace aegis::rhi::detail
{
/// @brief Number of mip levels in a full mip chain for an image of the given extent.
/// @note Free function rather than an Image member: it is pure extent arithmetic that needs no
///       image, which also keeps it unit-testable.
[[nodiscard]] auto calcMipLevels(Extent3D extent) -> std::uint32_t;

/// @brief Size in bytes of one texel of 'format'.
/// @return 0 for formats that cannot be copied from a linear buffer: Unknown, and the combined
///         depth/stencil formats (those need one copy region per aspect, which is not supported).
[[nodiscard]] constexpr auto bytesPerTexel(Format format) noexcept -> std::uint32_t;

/// @brief Extent of mip level 'level' of an image whose level 0 measures 'base'.
/// @note Each axis halves per level and clamps at 1, matching the Vulkan mip-chain definition.
[[nodiscard]] constexpr auto mipExtent(Extent3D base, std::uint32_t level) noexcept -> Extent3D;

/// @brief Buffer offset alignment that vkCmdCopyBufferToImage requires for 'format': a multiple of
///        both 4 and the texel size, rounded up to a power of two so it composes with alignTo.
[[nodiscard]] constexpr auto copyAlignment(Format format) noexcept -> std::size_t;

/// @brief Where one mip level of an image lives, both in the caller's data and in staging memory.
struct SubresourceFootprint
{
    std::uint32_t mipLevel;
    std::uint32_t arrayLayerCount;
    Extent3D extent;
    std::size_t sourceOffset;  //< Byte offset into the caller's tightly packed source data.
    std::size_t stagingOffset; //< Byte offset into the staging allocation.
    std::size_t size;          //< Bytes for this mip, covering all array layers.
};

/// @brief Lays out one footprint per mip level for an upload of the described image.
///
/// Source layout contract -- the caller's data must match this exactly, there is no way to detect a
/// mismatch at runtime:
///   - **Mip-major**: mip 0 first, then mip 1, and so on.
///   - **All array layers packed contiguously inside each mip level**, in layer order. This matches
///     KTX2 and lets each mip become a single copy region with layerCount = arrayLayers, so a
///     cubemap costs 1 region per mip rather than 6.
///   - **Tightly packed**: no padding between mips or layers.
///
/// 'stagingOffset' differs from 'sourceOffset' because Vulkan requires a copy's bufferOffset to be a
/// multiple of both 4 and the texel size; mips are padded up to that in staging, but never in the
/// caller's data. Callers must copy mip by mip rather than in one memcpy.
///
/// @return One footprint per mip level, or an empty vector if the format cannot be copied from a
///         linear buffer (see bytesPerTexel) or the description is degenerate.
[[nodiscard]] auto subresourceFootprints(Extent3D extent, Format format, std::uint32_t mipLevels,
    std::uint32_t arrayLayers) -> std::vector<SubresourceFootprint>;
}

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

    /// @note 'mipLevels' is passed separately because Desc::mipLevels may be the fullMipChain
    ///       sentinel; only create() knows the resolved count.
    Image(ImageAllocation allocation, const Desc& desc, std::uint32_t mipLevels);

    ImageAllocation m_allocation;
    Extent3D m_extent;
    Format m_format;
    ImageUsage m_usage;
    std::uint32_t m_mipLevels;
    std::uint32_t m_arrayLayers;
};
}
