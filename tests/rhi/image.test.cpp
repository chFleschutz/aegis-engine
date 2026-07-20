#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

import aegis.rhi;

TEST_SUITE("rhi::calcMipLevels")
{
    using aegis::rhi::Extent3D;
    using aegis::rhi::detail::calcMipLevels;

    TEST_CASE("a 1x1 image has a single mip level")
    {
        CHECK(calcMipLevels(Extent3D{ 1, 1, 1 }) == std::uint32_t{ 1 });
    }

    TEST_CASE("a power-of-two square image halves down to 1x1")
    {
        CHECK(calcMipLevels(Extent3D{ 2, 2, 1 }) == std::uint32_t{ 2 });
        CHECK(calcMipLevels(Extent3D{ 256, 256, 1 }) == std::uint32_t{ 9 });
        CHECK(calcMipLevels(Extent3D{ 1024, 1024, 1 }) == std::uint32_t{ 11 });
    }

    TEST_CASE("the largest dimension drives the chain length")
    {
        CHECK(calcMipLevels(Extent3D{ 1024, 512, 1 }) == std::uint32_t{ 11 });
        CHECK(calcMipLevels(Extent3D{ 512, 1024, 1 }) == std::uint32_t{ 11 });

        SUBCASE("including depth for 3D images")
        {
            CHECK(calcMipLevels(Extent3D{ 4, 4, 1024 }) == std::uint32_t{ 11 });
        }
    }

    TEST_CASE("a non-power-of-two extent rounds down to the enclosing power of two")
    {
        // floor(log2(100)) == 6, so 100 behaves like 64.
        CHECK(calcMipLevels(Extent3D{ 100, 100, 1 }) == std::uint32_t{ 7 });
        CHECK(calcMipLevels(Extent3D{ 5, 1, 1 }) == std::uint32_t{ 3 });
    }
}

TEST_SUITE("rhi::bytesPerTexel")
{
    using aegis::rhi::Format;
    using aegis::rhi::detail::bytesPerTexel;

    TEST_CASE("reports the byte size of a single texel")
    {
        CHECK(bytesPerTexel(Format::R8_UNORM) == std::uint32_t{ 1 });
        CHECK(bytesPerTexel(Format::RG8_UNORM) == std::uint32_t{ 2 });
        CHECK(bytesPerTexel(Format::RGBA8_UNORM) == std::uint32_t{ 4 });
        CHECK(bytesPerTexel(Format::RGBA8_SRGB) == std::uint32_t{ 4 });
        CHECK(bytesPerTexel(Format::RGBA16_SFLOAT) == std::uint32_t{ 8 });
        CHECK(bytesPerTexel(Format::RGB32_SFLOAT) == std::uint32_t{ 12 });
        CHECK(bytesPerTexel(Format::RGBA32_SFLOAT) == std::uint32_t{ 16 });
    }

    TEST_CASE("packed formats occupy their packed width, not their channel count")
    {
        CHECK(bytesPerTexel(Format::RGB10A2_UNORM) == std::uint32_t{ 4 });
        CHECK(bytesPerTexel(Format::B10G11R11_UFLOAT) == std::uint32_t{ 4 });
    }

    TEST_CASE("a format's bit width is per channel, not per texel")
    {
        // The Format enum groups these under "16-bit", which reads as a texel width but is not one.
        CHECK(bytesPerTexel(Format::R16_UNORM) == std::uint32_t{ 2 });
        CHECK(bytesPerTexel(Format::RG16_UNORM) == std::uint32_t{ 4 });
        CHECK(bytesPerTexel(Format::RGBA16_UNORM) == std::uint32_t{ 8 });

        SUBCASE("the same holds for the 32-bit family")
        {
            CHECK(bytesPerTexel(Format::R32_SFLOAT) == std::uint32_t{ 4 });
            CHECK(bytesPerTexel(Format::RG32_SFLOAT) == std::uint32_t{ 8 });
            CHECK(bytesPerTexel(Format::RGB32_SFLOAT) == std::uint32_t{ 12 });
            CHECK(bytesPerTexel(Format::RGBA32_SFLOAT) == std::uint32_t{ 16 });
        }
    }

    TEST_CASE("formats that cannot be copied from a linear buffer report zero")
    {
        CHECK(bytesPerTexel(Format::Unknown) == std::uint32_t{ 0 });

        SUBCASE("combined depth/stencil needs one region per aspect, which is unsupported")
        {
            CHECK(bytesPerTexel(Format::D24_UNORM_S8_UINT) == std::uint32_t{ 0 });
            CHECK(bytesPerTexel(Format::D32_SFLOAT_S8_UINT) == std::uint32_t{ 0 });
        }

        SUBCASE("but single-aspect depth is copyable")
        {
            CHECK(bytesPerTexel(Format::D32_SFLOAT) == std::uint32_t{ 4 });
        }
    }
}

TEST_SUITE("rhi::copyAlignment")
{
    using aegis::rhi::Format;
    using aegis::rhi::detail::bytesPerTexel;
    using aegis::rhi::detail::copyAlignment;

    TEST_CASE("is the least common multiple of 4 and the texel block size")
    {
        CHECK(copyAlignment(Format::R8_UNORM) == std::size_t{ 4 });
        CHECK(copyAlignment(Format::RG8_UNORM) == std::size_t{ 4 });
        CHECK(copyAlignment(Format::RGBA8_UNORM) == std::size_t{ 4 });
        CHECK(copyAlignment(Format::RGBA16_SFLOAT) == std::size_t{ 8 });
        CHECK(copyAlignment(Format::RGBA32_SFLOAT) == std::size_t{ 16 });
    }

    TEST_CASE("a 12-byte block aligns to 12, not to the next power of two")
    {
        CHECK(copyAlignment(Format::RGB32_SFLOAT) == std::size_t{ 12 });
    }

    TEST_CASE("every copyable format's alignment satisfies both Vulkan requirements")
    {
        constexpr std::array formats{
            Format::R8_UNORM, Format::RG8_UNORM, Format::RGBA8_UNORM, Format::RGBA8_SRGB,
            Format::BGRA8_UNORM, Format::BGRA8_SRGB, Format::RGB10A2_UNORM,
            Format::B10G11R11_UFLOAT, Format::R16_UNORM, Format::RG16_UNORM, Format::RGBA16_UNORM,
            Format::RGBA16_SFLOAT, Format::R32_SFLOAT, Format::RG32_SFLOAT, Format::RGB32_SFLOAT,
            Format::RGBA32_SFLOAT, Format::D32_SFLOAT
        };

        for (const auto format : formats)
        {
            CAPTURE(static_cast<int>(format));
            const auto alignment = copyAlignment(format);
            CHECK(alignment % std::size_t{ 4 } == std::size_t{ 0 });
            CHECK(alignment % std::size_t{ bytesPerTexel(format) } == std::size_t{ 0 });
        }
    }
}

TEST_SUITE("rhi::mipExtent")
{
    using aegis::rhi::Extent3D;
    using aegis::rhi::detail::mipExtent;

    TEST_CASE("level zero is the base extent")
    {
        CHECK(mipExtent(Extent3D{ 64, 32, 1 }, 0) == Extent3D{ 64, 32, 1 });
    }

    TEST_CASE("each level halves every axis")
    {
        CHECK(mipExtent(Extent3D{ 64, 32, 8 }, 1) == Extent3D{ 32, 16, 4 });
        CHECK(mipExtent(Extent3D{ 64, 32, 8 }, 3) == Extent3D{ 8, 4, 1 });
    }

    TEST_CASE("an axis clamps at 1 rather than collapsing to 0")
    {
        CHECK(mipExtent(Extent3D{ 8, 1, 1 }, 3) == Extent3D{ 1, 1, 1 });

        SUBCASE("a level past the end of the chain stays at 1x1x1")
        {
            CHECK(mipExtent(Extent3D{ 8, 8, 1 }, 10) == Extent3D{ 1, 1, 1 });
        }

        SUBCASE("a level wider than the operand does not shift out of range")
        {
            CHECK(mipExtent(Extent3D{ 8, 8, 1 }, 64) == Extent3D{ 1, 1, 1 });
        }
    }
}

TEST_SUITE("rhi::subresourceFootprints")
{
    using aegis::rhi::Extent3D;
    using aegis::rhi::Format;
    using aegis::rhi::detail::subresourceFootprints;

    TEST_CASE("a single-mip single-layer image yields one footprint at offset zero")
    {
        const auto footprints = subresourceFootprints(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 1, 1);

        REQUIRE(footprints.size() == std::size_t{ 1 });
        CHECK(footprints[0].mipLevel == std::uint32_t{ 0 });
        CHECK(footprints[0].arrayLayerCount == std::uint32_t{ 1 });
        CHECK(footprints[0].extent == Extent3D{ 4, 4, 1 });
        CHECK(footprints[0].sourceOffset == std::size_t{ 0 });
        CHECK(footprints[0].stagingOffset == std::size_t{ 0 });
        CHECK(footprints[0].size == std::size_t{ 64 });
    }

    TEST_CASE("a mip chain packs levels back to back in mip-major order")
    {
        // 4x4 RGBA8: 64 bytes, then 2x2 -> 16, then 1x1 -> 4.
        const auto footprints = subresourceFootprints(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 3, 1);

        REQUIRE(footprints.size() == std::size_t{ 3 });
        CHECK(footprints[0].sourceOffset == std::size_t{ 0 });
        CHECK(footprints[1].sourceOffset == std::size_t{ 64 });
        CHECK(footprints[2].sourceOffset == std::size_t{ 80 });
        CHECK(footprints[2].size == std::size_t{ 4 });

        SUBCASE("mip extents follow the chain")
        {
            CHECK(footprints[1].extent == Extent3D{ 2, 2, 1 });
            CHECK(footprints[2].extent == Extent3D{ 1, 1, 1 });
        }
    }

    TEST_CASE("array layers are packed inside each mip, not appended after the chain")
    {
        // 2x2 RGBA8 cubemap: mip 0 is 4 texels x 4 bytes x 6 faces, mip 1 is 1 x 4 x 6.
        const auto footprints = subresourceFootprints(Extent3D{ 2, 2, 1 }, Format::RGBA8_UNORM, 2, 6);

        REQUIRE(footprints.size() == std::size_t{ 2 });
        CHECK(footprints[0].arrayLayerCount == std::uint32_t{ 6 });
        CHECK(footprints[0].size == std::size_t{ 96 });
        CHECK(footprints[1].sourceOffset == std::size_t{ 96 });
        CHECK(footprints[1].size == std::size_t{ 24 });
    }

    TEST_CASE("staging offsets are padded to the copy alignment while source offsets stay tight")
    {
        // 3x3 R8: mip 0 is 9 bytes, which is not a legal bufferOffset for the next copy.
        const auto footprints = subresourceFootprints(Extent3D{ 3, 3, 1 }, Format::R8_UNORM, 2, 1);

        REQUIRE(footprints.size() == std::size_t{ 2 });
        CHECK(footprints[0].size == std::size_t{ 9 });
        CHECK(footprints[1].sourceOffset == std::size_t{ 9 });
        CHECK(footprints[1].stagingOffset == std::size_t{ 12 });
    }

    TEST_CASE("offsets are monotonic across a long chain")
    {
        const auto footprints = subresourceFootprints(Extent3D{ 256, 256, 1 }, Format::RGBA8_UNORM, 9, 1);

        REQUIRE(footprints.size() == std::size_t{ 9 });
        for (std::size_t i = 1; i < footprints.size(); ++i)
        {
            CHECK(footprints[i].sourceOffset ==
                  footprints[i - 1].sourceOffset + footprints[i - 1].size);
            CHECK(footprints[i].stagingOffset >=
                  footprints[i - 1].stagingOffset + footprints[i - 1].size);
        }
    }

    TEST_CASE("a 12-byte texel block keeps every staging offset a multiple of 12")
    {
        // R32G32B32 needs offsets that are a multiple of lcm(4, 12) == 12. Rounding to the next
        // power of two (16) satisfies neither the block size nor the spec.
        const auto footprints = subresourceFootprints(Extent3D{ 2, 1, 1 }, Format::RGB32_SFLOAT, 2, 1);

        REQUIRE(footprints.size() == std::size_t{ 2 });
        CHECK(footprints[0].size == std::size_t{ 24 });
        CHECK(footprints[1].size == std::size_t{ 12 });
        for (const auto& footprint : footprints)
            CHECK(footprint.stagingOffset % std::size_t{ 12 } == std::size_t{ 0 });
    }

    TEST_CASE("a format that cannot be copied from a linear buffer yields no footprints")
    {
        CHECK(subresourceFootprints(Extent3D{ 4, 4, 1 }, Format::D24_UNORM_S8_UINT, 1, 1).empty());
        CHECK(subresourceFootprints(Extent3D{ 4, 4, 1 }, Format::Unknown, 1, 1).empty());
    }

    TEST_CASE("a degenerate mip or layer count yields no footprints")
    {
        CHECK(subresourceFootprints(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 0, 1).empty());
        CHECK(subresourceFootprints(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 1, 0).empty());
    }
}
