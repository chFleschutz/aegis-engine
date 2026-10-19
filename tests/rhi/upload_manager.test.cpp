#include <doctest/doctest.h>

import std;
import aegis.rhi;

TEST_SUITE("rhi::planImageUpload")
{
    using aegis::rhi::ErrorCode;
    using aegis::rhi::Extent3D;
    using aegis::rhi::Format;
    using aegis::rhi::detail::planImageUpload;

    TEST_CASE("rejects data whose size does not match the mip chain")
    {
        // 4x4 RGBA8 with 3 mips: 64 + 16 + 4 bytes.
        CHECK(planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 3, 1, false, 84).has_value());

        for (const std::size_t size : { std::size_t{ 0 }, std::size_t{ 64 }, std::size_t{ 83 }, std::size_t{ 85 } })
        {
            CAPTURE(size);
            const auto plan = planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 3, 1, false, size);
            REQUIRE(!plan.has_value());
            CHECK(plan.error().code == ErrorCode::InvalidArgument);
        }
    }

    TEST_CASE("expects mip 0 only when the rest are generated")
    {
        const auto plan = planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 3, 1, true, 64);
        REQUIRE(plan.has_value());
        CHECK(plan->footprints.size() == 1);
        CHECK(plan->regions.size() == 1);
        CHECK(plan->generateMips);

        const auto fullChain = planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 3, 1, true, 84);
        REQUIRE(!fullChain.has_value());
        CHECK(fullChain.error().code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("an image with one mip has nothing to generate")
    {
        const auto plan = planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 1, 1, true, 64);
        REQUIRE(plan.has_value());
        CHECK(!plan->generateMips);
    }

    TEST_CASE("copies every array layer of a mip in one region, at its padded staging offset")
    {
        // 3x1 R8, 2 mips, 2 layers: mip 0 is 6 bytes, mip 1 is 2 bytes and starts at staging
        // offset 8 (the copy alignment is 4), but right after mip 0 in the caller's data.
        const auto plan = planImageUpload(Extent3D{ 3, 1, 1 }, Format::R8_UNORM, 2, 2, false, 8);
        REQUIRE(plan.has_value());
        REQUIRE(plan->regions.size() == 2);
        CHECK(plan->stagingAlignment == 4);
        CHECK(plan->stagingSize == 10);
        CHECK(plan->footprints[1].sourceOffset == 6);
        CHECK(plan->regions[0].bufferOffset == 0);
        CHECK(plan->regions[1].bufferOffset == 8);
        CHECK(plan->regions[1].mipLevel == 1);
        CHECK(plan->regions[1].arrayLayerCount == 2);
        CHECK(plan->regions[1].extent.x == 1);
    }

    TEST_CASE("rejects a format that cannot be copied from a buffer")
    {
        const auto plan = planImageUpload(Extent3D{ 4, 4, 1 }, Format::D24_UNORM_S8_UINT, 1, 1, false, 64);
        REQUIRE(!plan.has_value());
        CHECK(plan.error().code == ErrorCode::FormatNotSupported);
    }

    TEST_CASE("rejects an image without mips or array layers")
    {
        const auto noMips = planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 0, 1, false, 0);
        REQUIRE(!noMips.has_value());
        CHECK(noMips.error().code == ErrorCode::InvalidArgument);

        const auto noLayers = planImageUpload(Extent3D{ 4, 4, 1 }, Format::RGBA8_UNORM, 1, 0, false, 0);
        REQUIRE(!noLayers.has_value());
        CHECK(noLayers.error().code == ErrorCode::InvalidArgument);
    }
}
