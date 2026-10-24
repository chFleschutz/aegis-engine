#include <doctest/doctest.h>

import std;
import aegis.assets;

namespace
{
using namespace aegis;
using assets::ErrorCode;
using assets::TextureFormat;

// A width x height RGBA8 image whose texels are 'texel(x, y)'.
auto image(std::uint32_t width, std::uint32_t height, auto texel) -> assets::ImageData
{
    assets::ImageData result{ .width = width, .height = height };
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            for (const std::uint8_t value : texel(x, y))
                result.pixels.push_back(std::byte{ value });
        }
    }
    return result;
}

auto texelAt(const assets::TextureData& texture, std::uint32_t level, std::uint32_t x, std::uint32_t y) -> std::array<std::uint8_t, 4>
{
    const auto at = assets::mipLevelOffset(texture, level) + (static_cast<std::size_t>(y) * assets::mipExtent(texture.width, level) + x) * 4;
    std::array<std::uint8_t, 4> texel{};
    for (std::size_t c = 0; c < 4; ++c)
        texel[c] = std::to_integer<std::uint8_t>(texture.pixels[at + c]);
    return texel;
}
}

TEST_SUITE("assets::TextureData")
{
    TEST_CASE("a full mip chain halves down to one texel")
    {
        CHECK(assets::fullMipCount(1, 1) == 1);
        CHECK(assets::fullMipCount(2, 2) == 2);
        CHECK(assets::fullMipCount(256, 1) == 9);
        CHECK(assets::fullMipCount(300, 200) == 9);
        CHECK(assets::mipExtent(300, 8) == 1);
        CHECK(assets::mipExtent(300, 2) == 75);
    }

    TEST_CASE("the levels are laid out mip-major with the layers inside")
    {
        const assets::TextureData texture{ .format = TextureFormat::RGBA8Unorm, .width = 4, .height = 2, .mipLevels = 3, .arrayLayers = 6, .cube = true };
        CHECK(assets::mipLevelSize(texture, 0) == 4 * 2 * 6 * 4);
        CHECK(assets::mipLevelSize(texture, 1) == 2 * 1 * 6 * 4);
        CHECK(assets::mipLevelSize(texture, 2) == 1 * 1 * 6 * 4);
        CHECK(assets::mipLevelOffset(texture, 1) == 4 * 2 * 6 * 4);
        CHECK(assets::textureSize(texture) == (8 + 2 + 1) * 6 * 4);
    }
}

TEST_SUITE("assets::textureFromImage")
{
    TEST_CASE("each mip averages 2x2 texels of the one above")
    {
        const auto source = image(2, 2, [](std::uint32_t x, std::uint32_t y) {
            return std::array<std::uint8_t, 4>{ static_cast<std::uint8_t>(x * 100), static_cast<std::uint8_t>(y * 200), 10, 255 };
        });
        const auto texture = assets::textureFromImage(source, { .srgb = false });
        REQUIRE(texture);
        CHECK(texture->format == TextureFormat::RGBA8Unorm);
        CHECK(texture->mipLevels == 2);
        CHECK(texture->pixels.size() == (4 + 1) * 4);
        CHECK(std::ranges::equal(std::span{ texture->pixels }.first(16), source.pixels));
        CHECK(texelAt(*texture, 1, 0, 0) == std::array<std::uint8_t, 4>{ 50, 100, 10, 255 });
    }

    TEST_CASE("sRGB texels are averaged in linear space")
    {
        // Black and white average to linear 0.5, which is sRGB 188; alpha stays linear.
        const auto source = image(2, 1, [](std::uint32_t x, std::uint32_t) {
            const auto value = static_cast<std::uint8_t>(x * 255);
            return std::array<std::uint8_t, 4>{ value, value, value, value };
        });
        const auto srgb = assets::textureFromImage(source, { .srgb = true });
        REQUIRE(srgb);
        CHECK(srgb->format == TextureFormat::RGBA8Srgb);
        CHECK(texelAt(*srgb, 1, 0, 0) == std::array<std::uint8_t, 4>{ 188, 188, 188, 128 });
        const auto linear = assets::textureFromImage(source, { .srgb = false });
        REQUIRE(linear);
        CHECK(texelAt(*linear, 1, 0, 0) == std::array<std::uint8_t, 4>{ 128, 128, 128, 128 });
    }

    TEST_CASE("an odd size leaves out the last column")
    {
        const auto source = image(3, 1, [](std::uint32_t x, std::uint32_t) {
            const auto value = static_cast<std::uint8_t>(x == 2 ? 255 : 0);
            return std::array<std::uint8_t, 4>{ value, value, value, value };
        });
        const auto texture = assets::textureFromImage(source, { .srgb = false });
        REQUIRE(texture);
        CHECK(texture->mipLevels == 2);
        CHECK(texelAt(*texture, 1, 0, 0) == std::array<std::uint8_t, 4>{ 0, 0, 0, 0 });
    }

    TEST_CASE("without mips only mip 0 is kept")
    {
        const auto source = image(4, 4, [](std::uint32_t, std::uint32_t) { return std::array<std::uint8_t, 4>{ 1, 2, 3, 4 }; });
        const auto texture = assets::textureFromImage(source, { .mips = false });
        REQUIRE(texture);
        CHECK(texture->mipLevels == 1);
        CHECK(texture->pixels == source.pixels);
    }

    TEST_CASE("float images become RGBA32Float")
    {
        assets::ImageData source{ .width = 2, .height = 1, .format = assets::PixelFormat::RGBA32F };
        const std::array<float, 8> values{ 1.0f, 2.0f, 3.0f, 4.0f, 3.0f, 4.0f, 5.0f, 6.0f };
        const auto bytes = std::as_bytes(std::span{ values });
        source.pixels.assign(bytes.begin(), bytes.end());
        const auto texture = assets::textureFromImage(source, {});
        REQUIRE(texture);
        CHECK(texture->format == TextureFormat::RGBA32Float);
        REQUIRE(texture->mipLevels == 2);
        std::array<float, 4> mip1{};
        std::memcpy(mip1.data(), texture->pixels.data() + assets::mipLevelOffset(*texture, 1), sizeof(mip1));
        CHECK(mip1 == std::array{ 2.0f, 3.0f, 4.0f, 5.0f });
    }

    TEST_CASE("an empty or mismatched image is an error")
    {
        CHECK(assets::textureFromImage({}, {}).error().code == ErrorCode::InvalidArgument);
        const assets::ImageData shortImage{ .width = 2, .height = 2, .pixels = std::vector<std::byte>(4) };
        CHECK(assets::textureFromImage(shortImage, {}).error().code == ErrorCode::InvalidArgument);
    }
}
