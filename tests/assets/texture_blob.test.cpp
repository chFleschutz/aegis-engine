#include <doctest/doctest.h>

import std;
import aegis.assets;

namespace
{
using namespace aegis;
using assets::ErrorCode;
using assets::TextureFormat;

// A texture whose bytes count up, so a level or layer out of place changes them.
auto texture(TextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t mips, std::uint32_t layers, bool cube = false)
    -> assets::TextureData
{
    assets::TextureData result{ .format = format, .width = width, .height = height, .mipLevels = mips, .arrayLayers = layers, .cube = cube };
    result.pixels.resize(assets::textureSize(result));
    for (std::size_t i = 0; i < result.pixels.size(); ++i)
        result.pixels[i] = static_cast<std::byte>(i * 7 + i / 251);
    return result;
}

auto roundTrip(const assets::TextureData& data) -> assets::TextureData
{
    const auto blob = assets::writeTextureBlob(data, 0x0123456789abcdef);
    REQUIRE(blob);
    auto read = assets::readTextureBlob(*blob);
    REQUIRE(read);
    return std::move(*read);
}
}

TEST_SUITE("assets::TextureBlob")
{
    TEST_CASE("a 2D texture with mips round-trips")
    {
        const auto data = texture(TextureFormat::RGBA8Srgb, 8, 4, 4, 1);
        CHECK(roundTrip(data) == data);
    }

    TEST_CASE("a cube round-trips")
    {
        const auto data = texture(TextureFormat::RGBA32Float, 4, 4, 3, 6, true);
        CHECK(roundTrip(data) == data);
    }

    TEST_CASE("an array round-trips")
    {
        const auto data = texture(TextureFormat::RGBA8Unorm, 4, 2, 2, 3);
        CHECK(roundTrip(data) == data);
    }

    TEST_CASE("the header holds the key, format, extent, mips and layers")
    {
        const auto blob = assets::writeTextureBlob(texture(TextureFormat::RGBA8Srgb, 16, 16, 5, 6, true), 42);
        REQUIRE(blob);
        CHECK(std::ranges::equal(std::span{ *blob }.first(12),
            std::array<std::uint8_t, 12>{ 0xab, 'K', 'T', 'X', ' ', '2', '0', 0xbb, '\r', '\n', 0x1a, '\n' },
            [](std::byte a, std::uint8_t b) { return std::to_integer<std::uint8_t>(a) == b; }));
        const auto header = assets::readTextureBlobHeader(*blob);
        REQUIRE(header);
        CHECK(header->key == 42);
        CHECK(header->vkFormat == 43);
        CHECK(header->format == TextureFormat::RGBA8Srgb);
        CHECK(header->width == 16);
        CHECK(header->height == 16);
        CHECK(header->mipLevels == 5);
        CHECK(header->arrayLayers == 6);
        CHECK(header->cube);
        CHECK(header->zstd);
    }

    TEST_CASE("the same texture gives the same bytes")
    {
        const auto data = texture(TextureFormat::RGBA8Unorm, 8, 8, 4, 1);
        CHECK(*assets::writeTextureBlob(data, 1) == *assets::writeTextureBlob(data, 1));
        CHECK(*assets::writeTextureBlob(data, 1) != *assets::writeTextureBlob(data, 2));
    }

    TEST_CASE("each format has its VkFormat")
    {
        CHECK(assets::textureVkFormat(TextureFormat::RGBA8Unorm) == 37);
        CHECK(assets::textureVkFormat(TextureFormat::RGBA8Srgb) == 43);
        CHECK(assets::textureVkFormat(TextureFormat::RGBA32Float) == 109);
    }

    TEST_CASE("a texture that does not match its size is not written")
    {
        auto data = texture(TextureFormat::RGBA8Unorm, 4, 4, 3, 1);
        data.pixels.pop_back();
        CHECK(assets::writeTextureBlob(data, 1).error().code == ErrorCode::InvalidArgument);
        CHECK(assets::writeTextureBlob(texture(TextureFormat::RGBA8Unorm, 4, 4, 1, 4, true), 1).error().code == ErrorCode::InvalidArgument);
        CHECK(assets::writeTextureBlob(texture(TextureFormat::RGBA8Unorm, 4, 2, 1, 6, true), 1).error().code == ErrorCode::InvalidArgument);
        auto tooManyMips = texture(TextureFormat::RGBA8Unorm, 4, 4, 3, 1);
        tooManyMips.mipLevels = 4;
        CHECK(assets::writeTextureBlob(tooManyMips, 1).error().code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("other bytes are no texture blob")
    {
        const std::array junk{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
        CHECK(assets::readTextureBlobHeader(junk).error().code == ErrorCode::DecodeFailed);
        CHECK(assets::readTextureBlob({}).error().code == ErrorCode::DecodeFailed);
    }
}
