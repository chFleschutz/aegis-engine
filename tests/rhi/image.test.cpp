#include <doctest/doctest.h>

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
