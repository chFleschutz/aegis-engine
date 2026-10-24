export module aegis.assets:texture_data;
import std;
import :error;
import :image_data;

// A texture as the import cache keeps it (see :texture_blob): every mip level and layer of the image, in the
// format it is sampled in. Plain data without GPU state; Texture::create(TextureData) uploads it as it is.

export namespace aegis::assets
{
enum class TextureFormat
{
    RGBA8Unorm,  // 4 x 8-bit unsigned normalized: linear data (normals, metal/roughness, occlusion)
    RGBA8Srgb,   // 4 x 8-bit sRGB: color data (base color, emissive)
    RGBA32Float, // 4 x 32-bit float: HDR data (environment maps)
};

/// @brief Bytes of one texel.
[[nodiscard]] constexpr auto bytesPerTexel(TextureFormat format) noexcept -> std::size_t
{
    return format == TextureFormat::RGBA32Float ? 16 : 4;
}

[[nodiscard]] constexpr auto textureFormatName(TextureFormat format) noexcept -> std::string_view
{
    switch (format)
    {
    case TextureFormat::RGBA8Unorm: return "RGBA8Unorm";
    case TextureFormat::RGBA8Srgb: return "RGBA8Srgb";
    case TextureFormat::RGBA32Float: return "RGBA32Float";
    }
    return "Unknown";
}

struct TextureData
{
    TextureFormat format{ TextureFormat::RGBA8Unorm };
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };
    std::uint32_t mipLevels{ 1 };
    /// @brief Layers of the image; a multiple of 6 for a cube (one per face, +X, -X, +Y, -Y, +Z, -Z).
    std::uint32_t arrayLayers{ 1 };
    bool cube{ false };
    /// @brief Mip-major, the layers packed inside each mip, rows tightly packed, top row first: the layout
    ///        rhi::Device::upload takes.
    std::vector<std::byte> pixels;

    auto operator==(const TextureData&) const -> bool = default;
};

/// @brief The size of mip 'level' of a texture 'size' texels wide (or high): halved per level, at least 1.
[[nodiscard]] constexpr auto mipExtent(std::uint32_t size, std::uint32_t level) noexcept -> std::uint32_t
{
    return std::max(size >> level, 1u);
}

/// @brief The number of levels of a full mip chain of a 'width' x 'height' texture.
[[nodiscard]] constexpr auto fullMipCount(std::uint32_t width, std::uint32_t height) noexcept -> std::uint32_t
{
    return static_cast<std::uint32_t>(std::bit_width(std::max({ width, height, 1u })));
}

/// @brief The bytes of mip 'level' of 'texture', all layers.
[[nodiscard]] auto mipLevelSize(const TextureData& texture, std::uint32_t level) -> std::size_t;

/// @brief The bytes 'texture' must have: every mip level of every layer.
[[nodiscard]] auto textureSize(const TextureData& texture) -> std::size_t;

/// @brief Where mip 'level' of 'texture' starts in TextureData::pixels.
[[nodiscard]] auto mipLevelOffset(const TextureData& texture, std::uint32_t level) -> std::size_t;

struct TextureFromImageDesc
{
    /// @brief RGBA8 data is color data (sRGB): mips average it in linear space.
    bool srgb{ false };
    /// @brief A full mip chain, each level the 2x2 average of the one above (the last row or column of an odd
    ///        size is left out); else mip 0 only.
    bool mips{ true };
};

/// @brief A 2D texture from a decoded image: RGBA8Srgb or RGBA8Unorm for RGBA8 data, RGBA32Float for RGBA32F
///        data, with the mips 'desc' asks for.
/// @return ErrorCode::InvalidArgument for an empty image or one whose pixels do not match its size.
[[nodiscard]] auto textureFromImage(const ImageData& image, const TextureFromImageDesc& desc) -> std::expected<TextureData, Error>;
}
