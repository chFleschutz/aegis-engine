#include <doctest/doctest.h>

import std;
import aegis.assets;

namespace
{
using namespace aegis;

// A 2x2 RGBA PNG: red, green (alpha 128) / blue, (10, 20, 30, alpha 0).
constexpr std::array<std::uint8_t, 80> pngBytes{ 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6,
    0x0d, 0x24, 0x00, 0x00, 0x00, 0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x08,
    0x1b, 0x18, 0x80, 0xf4, 0x7f, 0x2e, 0x11, 0x39, 0x06, 0x00, 0x3b, 0x7c, 0x05, 0xb8, 0x43, 0xcc, 0xa5, 0x25, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };

auto toBytes(std::string_view text) -> std::vector<std::byte>
{
    return std::as_bytes(std::span{ text }) | std::ranges::to<std::vector>();
}

// A 2x1 Radiance HDR (flat RGBE): (1, 1, 1) and (0.25, 0.5, 255/256).
auto hdrBytes() -> std::vector<std::byte>
{
    auto bytes = toBytes("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n");
    for (const auto value : { 128, 128, 128, 129, 64, 128, 255, 128 })
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

auto pngData() -> std::vector<std::byte>
{
    return std::as_bytes(std::span{ pngBytes }) | std::ranges::to<std::vector>();
}

auto floatAt(const assets::ImageData& image, std::size_t index) -> float
{
    float value{ 0 };
    std::memcpy(&value, image.pixels.data() + index * sizeof(float), sizeof(float));
    return value;
}
}

TEST_SUITE("assets::decodeImage")
{
    TEST_CASE("decodes a PNG to RGBA8")
    {
        const auto image = assets::decodeImage(pngData(), "tiny.png");
        REQUIRE(image.has_value());
        CHECK(image->width == 2);
        CHECK(image->height == 2);
        CHECK(image->format == assets::PixelFormat::RGBA8);
        REQUIRE(image->pixels.size() == 16);

        const std::array<std::uint8_t, 16> expected{ 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 10, 20, 30, 0 };
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(std::to_integer<std::uint8_t>(image->pixels[i]) == expected[i]);
    }

    TEST_CASE("decodes a Radiance HDR to RGBA32F")
    {
        const auto image = assets::decodeImage(hdrBytes(), "tiny.hdr");
        REQUIRE(image.has_value());
        CHECK(image->width == 2);
        CHECK(image->height == 1);
        CHECK(image->format == assets::PixelFormat::RGBA32F);
        REQUIRE(image->pixels.size() == 32);

        CHECK(floatAt(*image, 0) == doctest::Approx(1.0f));
        CHECK(floatAt(*image, 3) == doctest::Approx(1.0f)); // alpha
        CHECK(floatAt(*image, 4) == doctest::Approx(0.25f));
        CHECK(floatAt(*image, 5) == doctest::Approx(0.5f));
        CHECK(floatAt(*image, 6) == doctest::Approx(255.0f / 256.0f));
    }

    TEST_CASE("converts to the requested pixel format")
    {
        const auto asFloat = assets::decodeImage(pngData(), "tiny.png", assets::PixelFormat::RGBA32F);
        REQUIRE(asFloat.has_value());
        CHECK(asFloat->format == assets::PixelFormat::RGBA32F);
        CHECK(asFloat->pixels.size() == 2 * 2 * assets::bytesPerPixel(assets::PixelFormat::RGBA32F));

        const auto asBytes = assets::decodeImage(hdrBytes(), "tiny.hdr", assets::PixelFormat::RGBA8);
        REQUIRE(asBytes.has_value());
        CHECK(asBytes->format == assets::PixelFormat::RGBA8);
        CHECK(asBytes->pixels.size() == 2 * 1 * 4);
    }

    TEST_CASE("rejects empty data")
    {
        const auto image = assets::decodeImage({}, "empty.png");
        REQUIRE_FALSE(image.has_value());
        CHECK(image.error().code == assets::ErrorCode::DecodeFailed);
        CHECK(image.error().message.contains("empty.png"));
    }

    TEST_CASE("rejects bytes that are no image")
    {
        const auto image = assets::decodeImage(toBytes("this is not an image"), "text.png");
        REQUIRE_FALSE(image.has_value());
        CHECK(image.error().code == assets::ErrorCode::DecodeFailed);
    }

    TEST_CASE("rejects a truncated PNG")
    {
        auto png = pngData();
        png.resize(png.size() / 2);
        const auto image = assets::decodeImage(png, "cut.png");
        REQUIRE_FALSE(image.has_value());
        CHECK(image.error().code == assets::ErrorCode::DecodeFailed);
        CHECK(assets::describe(image.error()).starts_with("DecodeFailed: "));
    }
}

TEST_SUITE("assets::decodeImageFile")
{
    TEST_CASE("reports a missing file")
    {
        const auto image = assets::decodeImageFile("does/not/exist.png");
        REQUIRE_FALSE(image.has_value());
        CHECK(image.error().code == assets::ErrorCode::FileNotFound);
    }

    TEST_CASE("decodes a file whatever the case of its extension")
    {
        const auto path = std::filesystem::temp_directory_path() / "aegis_image_data_test.PNG";
        {
            std::ofstream file{ path, std::ios::binary };
            file.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));
        }
        const auto image = assets::decodeImageFile(path);
        std::filesystem::remove(path);
        REQUIRE(image.has_value());
        CHECK(image->width == 2);
    }
}

TEST_SUITE("assets::ImageData")
{
    TEST_CASE("is a comparable value type")
    {
        const auto a = assets::decodeImage(pngData());
        auto b = assets::decodeImage(pngData());
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(*a == *b);
        b->pixels[0] = std::byte{ 1 };
        CHECK_FALSE(*a == *b);
    }
}
