module;

#include <ktx.h>

module aegis.assets;
import std;

namespace aegis::assets
{
namespace
{
// VkFormat values (the Vulkan headers are the RHI's alone).
constexpr std::uint32_t vkFormatR8G8B8A8Unorm{ 37 };
constexpr std::uint32_t vkFormatR8G8B8A8Srgb{ 43 };
constexpr std::uint32_t vkFormatR32G32B32A32Sfloat{ 109 };

struct KtxDeleter
{
    auto operator()(ktxTexture2* texture) const noexcept -> void { ktxTexture_Destroy(reinterpret_cast<ktxTexture*>(texture)); }
};
using KtxTexture = std::unique_ptr<ktxTexture2, KtxDeleter>;

auto base(const KtxTexture& texture) -> ktxTexture*
{
    return reinterpret_cast<ktxTexture*>(texture.get());
}

auto ktxError(ErrorCode code, std::string_view what, KTX_error_code result) -> std::unexpected<Error>
{
    return makeError(code, std::format("{}: {}", what, ktxErrorString(result)));
}

auto formatOf(std::uint32_t vkFormat) -> std::optional<TextureFormat>
{
    switch (vkFormat)
    {
    case vkFormatR8G8B8A8Unorm: return TextureFormat::RGBA8Unorm;
    case vkFormatR8G8B8A8Srgb: return TextureFormat::RGBA8Srgb;
    case vkFormatR32G32B32A32Sfloat: return TextureFormat::RGBA32Float;
    default: return std::nullopt;
    }
}

auto parseKey(std::string_view text) -> std::optional<std::uint64_t>
{
    std::uint64_t key{ 0 };
    if (text.size() != 16 || !std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        return std::nullopt;
    std::from_chars(text.data(), text.data() + text.size(), key, 16);
    return key;
}

// The texture of 'blob' (its levels not yet loaded) and its checked header.
auto open(std::span<const std::byte> blob) -> std::expected<std::pair<KtxTexture, TextureBlobHeader>, Error>
{
    ktxTexture2* raw{ nullptr };
    if (const auto result = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(blob.data()), blob.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &raw);
        result != KTX_SUCCESS)
    {
        return ktxError(ErrorCode::DecodeFailed, "Not a KTX2 texture", result);
    }
    KtxTexture texture{ raw };

    const auto format = formatOf(texture->vkFormat);
    if (!format)
        return makeError(ErrorCode::UnsupportedFormat, std::format("A texture of VkFormat {} is not supported", texture->vkFormat));
    if (texture->numDimensions != 2 || texture->baseDepth != 1)
        return makeError(ErrorCode::UnsupportedFormat, std::format("A {}D texture is not supported", texture->numDimensions));
    if (texture->supercompressionScheme != KTX_SS_NONE && texture->supercompressionScheme != KTX_SS_ZSTD)
        return makeError(ErrorCode::UnsupportedFormat, "Only zstd supercompression is supported");
    if (texture->numFaces != 1 && texture->numFaces != 6)
        return makeError(ErrorCode::UnsupportedFormat, std::format("A texture of {} faces is not supported", texture->numFaces));

    char* value{ nullptr };
    unsigned int length{ 0 };
    if (ktxHashList_FindValue(&texture->kvDataHead, textureBlobKeyName.data(), &length, reinterpret_cast<void**>(&value)) != KTX_SUCCESS)
        return makeError(ErrorCode::InvalidData, std::format("The texture has no '{}' entry", textureBlobKeyName));
    // The value is written with its terminating zero.
    const auto key = parseKey(std::string_view{ value, length }.substr(0, length > 0 ? length - 1 : 0));
    if (!key)
        return makeError(ErrorCode::InvalidData, std::format("The texture's '{}' entry is no key", textureBlobKeyName));

    TextureBlobHeader header{
        .key = *key,
        .vkFormat = texture->vkFormat,
        .format = *format,
        .width = texture->baseWidth,
        .height = texture->baseHeight,
        .mipLevels = texture->numLevels,
        .arrayLayers = texture->numLayers * texture->numFaces,
        .cube = texture->numFaces == 6,
        .zstd = texture->supercompressionScheme == KTX_SS_ZSTD,
    };
    return std::pair{ std::move(texture), header };
}
}

auto textureVkFormat(TextureFormat format) noexcept -> std::uint32_t
{
    switch (format)
    {
    case TextureFormat::RGBA8Unorm: return vkFormatR8G8B8A8Unorm;
    case TextureFormat::RGBA8Srgb: return vkFormatR8G8B8A8Srgb;
    case TextureFormat::RGBA32Float: return vkFormatR32G32B32A32Sfloat;
    }
    return 0;
}

auto writeTextureBlob(const TextureData& texture, std::uint64_t key) -> std::expected<std::vector<std::byte>, Error>
{
    const auto faces = texture.cube ? 6u : 1u;
    if (texture.width == 0 || texture.height == 0 || texture.mipLevels == 0 || texture.arrayLayers == 0 || texture.arrayLayers % faces != 0)
        return makeError(ErrorCode::InvalidArgument, "A texture needs a size, mips and layers (a multiple of 6 for a cube)");
    if (texture.cube && texture.width != texture.height)
        return makeError(ErrorCode::InvalidArgument, std::format("A cube of {} x {} is not square", texture.width, texture.height));
    if (texture.mipLevels > fullMipCount(texture.width, texture.height))
        return makeError(ErrorCode::InvalidArgument, std::format("A {} x {} texture has no {} mips", texture.width, texture.height, texture.mipLevels));
    if (texture.pixels.size() != textureSize(texture))
        return makeError(ErrorCode::InvalidArgument, std::format("The texture has {} bytes, not {}", texture.pixels.size(), textureSize(texture)));

    const auto layers = texture.arrayLayers / faces;
    ktxTextureCreateInfo info{
        .glInternalformat = 0,
        .vkFormat = textureVkFormat(texture.format),
        .pDfd = nullptr,
        .baseWidth = texture.width,
        .baseHeight = texture.height,
        .baseDepth = 1,
        .numDimensions = 2,
        .numLevels = texture.mipLevels,
        .numLayers = layers,
        .numFaces = faces,
        .isArray = layers > 1 ? KTX_TRUE : KTX_FALSE,
        .generateMipmaps = KTX_FALSE,
    };
    ktxTexture2* raw{ nullptr };
    if (const auto result = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw); result != KTX_SUCCESS)
        return ktxError(ErrorCode::InvalidData, "Cannot create the KTX2 texture", result);
    KtxTexture ktx{ raw };

    // TextureData packs the layers (faces inside each layer) inside each mip, as KTX2 does.
    for (std::uint32_t level = 0; level < texture.mipLevels; ++level)
    {
        const auto imageSize = mipLevelSize(texture, level) / texture.arrayLayers;
        auto offset = mipLevelOffset(texture, level);
        for (std::uint32_t layer = 0; layer < layers; ++layer)
        {
            for (std::uint32_t face = 0; face < faces; ++face, offset += imageSize)
            {
                if (const auto result = ktxTexture_SetImageFromMemory(base(ktx), level, layer, face,
                        reinterpret_cast<const ktx_uint8_t*>(texture.pixels.data() + offset), imageSize);
                    result != KTX_SUCCESS)
                {
                    return ktxError(ErrorCode::InvalidData, "Cannot fill the KTX2 texture", result);
                }
            }
        }
    }

    const auto keyText = std::format("{:016x}", key);
    if (const auto result = ktxHashList_AddKVPair(&ktx->kvDataHead, textureBlobKeyName.data(), static_cast<unsigned int>(keyText.size() + 1), keyText.c_str());
        result != KTX_SUCCESS)
    {
        return ktxError(ErrorCode::InvalidData, "Cannot add the key to the KTX2 texture", result);
    }
    constexpr std::string_view writer{ "aegis-engine" };
    if (const auto result = ktxHashList_AddKVPair(&ktx->kvDataHead, KTX_WRITER_KEY, static_cast<unsigned int>(writer.size() + 1), writer.data());
        result != KTX_SUCCESS)
    {
        return ktxError(ErrorCode::InvalidData, "Cannot add the writer to the KTX2 texture", result);
    }
    if (const auto result = ktxTexture2_DeflateZstd(ktx.get(), textureBlobZstdLevel); result != KTX_SUCCESS)
        return ktxError(ErrorCode::InvalidData, "Cannot compress the KTX2 texture", result);

    ktx_uint8_t* bytes{ nullptr };
    ktx_size_t size{ 0 };
    if (const auto result = ktxTexture_WriteToMemory(base(ktx), &bytes, &size); result != KTX_SUCCESS)
        return ktxError(ErrorCode::InvalidData, "Cannot write the KTX2 texture", result);
    const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> owned{ bytes, &std::free };
    const auto* first = reinterpret_cast<const std::byte*>(bytes);
    return std::vector<std::byte>(first, first + size);
}

auto readTextureBlobHeader(std::span<const std::byte> blob) -> std::expected<TextureBlobHeader, Error>
{
    auto opened = open(blob);
    if (!opened)
        return std::unexpected{ std::move(opened.error()) };
    return opened->second;
}

auto readTextureBlob(std::span<const std::byte> blob) -> std::expected<TextureData, Error>
{
    auto opened = open(blob);
    if (!opened)
        return std::unexpected{ std::move(opened.error()) };
    auto& [ktx, header] = *opened;
    if (const auto result = ktxTexture2_LoadImageData(ktx.get(), nullptr, 0); result != KTX_SUCCESS)
        return ktxError(ErrorCode::DecodeFailed, "Cannot read the levels of the KTX2 texture", result);

    TextureData texture{
        .format = header.format,
        .width = header.width,
        .height = header.height,
        .mipLevels = header.mipLevels,
        .arrayLayers = header.arrayLayers,
        .cube = header.cube,
    };
    texture.pixels.resize(textureSize(texture));
    const auto faces = ktx->numFaces;
    const std::span<const std::byte> data{ reinterpret_cast<const std::byte*>(ktxTexture_GetData(base(ktx))), ktxTexture_GetDataSize(base(ktx)) };
    for (std::uint32_t level = 0; level < texture.mipLevels; ++level)
    {
        const auto imageSize = mipLevelSize(texture, level) / texture.arrayLayers;
        if (ktxTexture_GetImageSize(base(ktx), level) != imageSize)
            return makeError(ErrorCode::InvalidData, std::format("Level {} of the KTX2 texture has another size", level));
        auto offset = mipLevelOffset(texture, level);
        for (std::uint32_t layer = 0; layer < ktx->numLayers; ++layer)
        {
            for (std::uint32_t face = 0; face < faces; ++face, offset += imageSize)
            {
                ktx_size_t source{ 0 };
                if (const auto result = ktxTexture_GetImageOffset(base(ktx), level, layer, face, &source); result != KTX_SUCCESS || source + imageSize > data.size())
                    return makeError(ErrorCode::InvalidData, std::format("Level {} of the KTX2 texture is cut short", level));
                std::ranges::copy(data.subspan(source, imageSize), texture.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
        }
    }
    return texture;
}
}
