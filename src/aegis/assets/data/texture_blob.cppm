export module aegis.assets:texture_blob;
import std;
import :error;
import :texture_data;

// The cooked form of a texture, as the import cache keeps it and export packs it: a KTX2 file (libktx) with
// every level of TextureData, its VkFormat, zstd supercompression and the import cache key in its key/value data,
// so standard tools (`ktx info`) read it.

export namespace aegis::assets
{
/// @brief The KTX2 key/value entry holding the import cache key (16 lowercase hex digits).
inline constexpr std::string_view textureBlobKeyName{ "aegis.cacheKey" };
/// @brief The zstd level the levels are compressed with.
inline constexpr std::uint32_t textureBlobZstdLevel{ 3 };

/// @brief What a texture blob's header and key/value data say, without its levels.
struct TextureBlobHeader
{
    std::uint64_t key{ 0 }; // The import cache key of the source it was made from
    std::uint32_t vkFormat{ 0 };
    TextureFormat format{ TextureFormat::RGBA8Unorm };
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };
    std::uint32_t mipLevels{ 0 };
    std::uint32_t arrayLayers{ 0 }; // Faces included, as TextureData counts them
    bool cube{ false };
    bool zstd{ false }; // Supercompressed with zstd; else not supercompressed
};

/// @brief The VkFormat value of 'format', as KTX2 stores it.
[[nodiscard]] auto textureVkFormat(TextureFormat format) noexcept -> std::uint32_t;

/// @brief 'texture' as a KTX2 file made from the source with import cache key 'key', zstd-compressed.
/// @return ErrorCode::InvalidArgument if the pixels do not match the size, layers and mips (a cube is square and
///         needs a multiple of 6 layers), ErrorCode::InvalidData if libktx fails.
[[nodiscard]] auto writeTextureBlob(const TextureData& texture, std::uint64_t key) -> std::expected<std::vector<std::byte>, Error>;

/// @brief The header of a texture blob, checked: a KTX2 file of a 2D image in one of the TextureFormat formats,
///        with the key entry.
/// @return ErrorCode::DecodeFailed if it is no KTX2 file, ErrorCode::UnsupportedFormat for another format,
///         dimension, depth or supercompression, ErrorCode::InvalidData without the key entry.
[[nodiscard]] auto readTextureBlobHeader(std::span<const std::byte> blob) -> std::expected<TextureBlobHeader, Error>;

/// @brief The texture a blob holds, decompressed. The errors of readTextureBlobHeader.
[[nodiscard]] auto readTextureBlob(std::span<const std::byte> blob) -> std::expected<TextureData, Error>;
}
