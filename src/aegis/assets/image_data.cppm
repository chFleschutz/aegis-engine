export module aegis.assets:image_data;
import std;
import :error;

export namespace aegis::assets
{
enum class PixelFormat
{
    RGBA8,  // 4 x 8-bit unsigned normalized, as stored in LDR files (PNG, JPEG, ...)
    RGBA32F // 4 x 32-bit float, as decoded from HDR files (Radiance .hdr)
};

/// @brief Bytes of one pixel.
[[nodiscard]] constexpr auto bytesPerPixel(PixelFormat format) noexcept -> std::size_t
{
    return format == PixelFormat::RGBA32F ? 16 : 4;
}

/// @brief A decoded 2D image: tightly packed rows, top row first, four channels per pixel.
/// @note A plain value type without any GPU state, so it can be cached or serialized as it is.
struct ImageData
{
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };
    PixelFormat format{ PixelFormat::RGBA8 };
    std::vector<std::byte> pixels; // width * height * bytesPerPixel(format) bytes

    auto operator==(const ImageData&) const -> bool = default;
};

/// @brief Decodes an encoded image (PNG, JPEG, BMP, TGA, Radiance HDR, ...) held in memory. The format
///        is detected from the contents.
/// @param name Names the image in error messages, e.g. its file name.
/// @param format The pixel format to produce; by default RGBA32F for HDR data and RGBA8 for the rest.
///        Converting between them follows stb_image (HDR to 8 bit is tone-mapped, 8 bit to float is
///        gamma-corrected).
/// @return ErrorCode::DecodeFailed if the data is empty, truncated or in an unknown format.
[[nodiscard]] auto decodeImage(std::span<const std::byte> encoded, std::string_view name = {},
    std::optional<PixelFormat> format = std::nullopt) -> std::expected<ImageData, Error>;

/// @brief Reads and decodes an image file; see decodeImage.
/// @return ErrorCode::FileNotFound if 'path' does not exist.
[[nodiscard]] auto decodeImageFile(const std::filesystem::path& path, std::optional<PixelFormat> format = std::nullopt)
    -> std::expected<ImageData, Error>;
}
