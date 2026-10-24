module aegis.assets;
import std;

namespace aegis::assets
{
namespace
{
auto srgbToLinear(float value) -> float
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

auto linearToSrgb(float value) -> float
{
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

// The linear value of each 8-bit sRGB value.
auto srgbTable() -> const std::array<float, 256>&
{
    static const auto table = [] {
        std::array<float, 256> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = srgbToLinear(static_cast<float>(i) / 255.0f);
        return values;
    }();
    return table;
}

auto toUnorm8(float value) -> std::byte
{
    return static_cast<std::byte>(static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
}

// 'dst' (dstWidth x dstHeight) as the 2x2 averages of 'src' (srcWidth wide); channel 3 is alpha, always linear.
auto downsample(TextureFormat format, std::span<const std::byte> src, std::uint32_t srcWidth, std::span<std::byte> dst,
    std::uint32_t dstWidth, std::uint32_t dstHeight, std::uint32_t srcHeight) -> void
{
    const auto texel = bytesPerTexel(format);
    for (std::uint32_t y = 0; y < dstHeight; ++y)
    {
        for (std::uint32_t x = 0; x < dstWidth; ++x)
        {
            // A source of size 1 has one texel to average along that axis.
            const std::array xs{ std::min(2 * x, srcWidth - 1), std::min(2 * x + 1, srcWidth - 1) };
            const std::array ys{ std::min(2 * y, srcHeight - 1), std::min(2 * y + 1, srcHeight - 1) };
            const auto out = (static_cast<std::size_t>(y) * dstWidth + x) * texel;
            for (std::size_t c = 0; c < 4; ++c)
            {
                float sum{ 0.0f };
                for (const auto sy : ys)
                {
                    for (const auto sx : xs)
                    {
                        const auto at = (static_cast<std::size_t>(sy) * srcWidth + sx) * texel;
                        if (format == TextureFormat::RGBA32Float)
                        {
                            float value{ 0.0f };
                            std::memcpy(&value, src.data() + at + c * sizeof(float), sizeof(float));
                            sum += value;
                        }
                        else
                        {
                            const auto byte = std::to_integer<std::uint8_t>(src[at + c]);
                            sum += format == TextureFormat::RGBA8Srgb && c < 3 ? srgbTable()[byte] : static_cast<float>(byte) / 255.0f;
                        }
                    }
                }
                const auto average = sum / 4.0f;
                if (format == TextureFormat::RGBA32Float)
                    std::memcpy(dst.data() + out + c * sizeof(float), &average, sizeof(float));
                else
                    dst[out + c] = toUnorm8(format == TextureFormat::RGBA8Srgb && c < 3 ? linearToSrgb(average) : average);
            }
        }
    }
}
}

auto mipLevelSize(const TextureData& texture, std::uint32_t level) -> std::size_t
{
    return static_cast<std::size_t>(mipExtent(texture.width, level)) * mipExtent(texture.height, level) * texture.arrayLayers *
           bytesPerTexel(texture.format);
}

auto textureSize(const TextureData& texture) -> std::size_t
{
    return mipLevelOffset(texture, texture.mipLevels);
}

auto mipLevelOffset(const TextureData& texture, std::uint32_t level) -> std::size_t
{
    std::size_t offset{ 0 };
    for (std::uint32_t l = 0; l < level; ++l)
        offset += mipLevelSize(texture, l);
    return offset;
}

auto textureFromImage(const ImageData& image, const TextureFromImageDesc& desc) -> std::expected<TextureData, Error>
{
    if (image.width == 0 || image.height == 0)
        return makeError(ErrorCode::InvalidArgument, "An empty image is no texture");
    const auto format = image.format == PixelFormat::RGBA32F ? TextureFormat::RGBA32Float
                        : desc.srgb                          ? TextureFormat::RGBA8Srgb
                                                             : TextureFormat::RGBA8Unorm;
    TextureData texture{
        .format = format,
        .width = image.width,
        .height = image.height,
        .mipLevels = desc.mips ? fullMipCount(image.width, image.height) : 1,
    };
    if (image.pixels.size() != mipLevelSize(texture, 0))
    {
        return makeError(ErrorCode::InvalidArgument,
            std::format("A {} x {} image has {} bytes, not {}", image.width, image.height, image.pixels.size(), mipLevelSize(texture, 0)));
    }

    texture.pixels.resize(textureSize(texture));
    std::ranges::copy(image.pixels, texture.pixels.begin());
    for (std::uint32_t level = 1; level < texture.mipLevels; ++level)
    {
        const std::span<const std::byte> src{ texture.pixels.data() + mipLevelOffset(texture, level - 1), mipLevelSize(texture, level - 1) };
        const std::span<std::byte> dst{ texture.pixels.data() + mipLevelOffset(texture, level), mipLevelSize(texture, level) };
        downsample(format, src, mipExtent(texture.width, level - 1), dst, mipExtent(texture.width, level), mipExtent(texture.height, level),
            mipExtent(texture.height, level - 1));
    }
    return texture;
}
}
