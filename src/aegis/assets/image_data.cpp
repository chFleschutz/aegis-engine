module;
#include <stb/stb_image.h>

module aegis.assets;
import std;

namespace aegis::assets
{
namespace
{
// stbi_load* results are released through stbi_image_free.
struct StbiDeleter
{
    auto operator()(void* pixels) const noexcept -> void { stbi_image_free(pixels); }
};
}

auto decodeImage(std::span<const std::byte> encoded, std::string_view name, std::optional<PixelFormat> format)
    -> std::expected<ImageData, Error>
{
    if (encoded.empty())
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to decode '{}': no data", name));
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to decode '{}': data is too large", name));

    const auto* data = reinterpret_cast<const stbi_uc*>(encoded.data());
    const auto size = static_cast<int>(encoded.size());
    const auto target = format.value_or(stbi_is_hdr_from_memory(data, size) ? PixelFormat::RGBA32F : PixelFormat::RGBA8);

    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    const std::unique_ptr<void, StbiDeleter> decoded{ target == PixelFormat::RGBA32F
                                                          ? static_cast<void*>(stbi_loadf_from_memory(data, size, &width, &height, &channels, STBI_rgb_alpha))
                                                          : static_cast<void*>(stbi_load_from_memory(data, size, &width, &height, &channels, STBI_rgb_alpha)) };
    if (!decoded)
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to decode '{}': {}", name, stbi_failure_reason()));

    const auto byteCount = bytesPerPixel(target) * static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto* bytes = static_cast<const std::byte*>(decoded.get());
    return ImageData{
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
        .format = target,
        .pixels = std::vector<std::byte>(bytes, bytes + byteCount),
    };
}

auto decodeImageFile(const std::filesystem::path& path, std::optional<PixelFormat> format)
    -> std::expected<ImageData, Error>
{
    std::ifstream file{ path, std::ios::binary };
    if (!file)
        return makeError(ErrorCode::FileNotFound, std::format("Texture file does not exist: '{}'", path.string()));

    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError)
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to read '{}': {}", path.string(), sizeError.message()));

    std::vector<std::byte> encoded(fileSize);
    file.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!file)
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to read '{}'", path.string()));

    return decodeImage(encoded, path.string(), format);
}
}
