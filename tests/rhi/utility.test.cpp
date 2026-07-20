#include <doctest/doctest.h>

#include <cstddef>

import aegis.rhi;

TEST_SUITE("rhi::utility::alignTo")
{
    using aegis::rhi::utility::alignTo;

    TEST_CASE("rounds up to the next multiple of a power-of-two alignment")
    {
        CHECK(alignTo(1, 4) == 4);
        CHECK(alignTo(4, 4) == 4);
        CHECK(alignTo(5, 4) == 8);
        CHECK(alignTo(13, 8) == 16);
        CHECK(alignTo(64, 64) == 64);
        CHECK(alignTo(65, 64) == 128);
    }

    TEST_CASE("leaves already-aligned sizes unchanged")
    {
        CHECK(alignTo(0, 16) == 0);
        CHECK(alignTo(16, 16) == 16);
        CHECK(alignTo(256, 256) == 256);
    }

    TEST_CASE("an alignment of one is the identity")
    {
        CHECK(alignTo(0, 1) == 0);
        CHECK(alignTo(1, 1) == 1);
        CHECK(alignTo(1234, 1) == 1234);
    }
}

TEST_SUITE("rhi::utility::roundUpTo")
{
    using aegis::rhi::utility::roundUpTo;

    TEST_CASE("rounds up to the next multiple of a non-power-of-two")
    {
        // 12 is the texel block size of R32G32B32; rounding to 16 instead would land off-multiple.
        CHECK(roundUpTo(1, 12) == 12);
        CHECK(roundUpTo(12, 12) == 12);
        CHECK(roundUpTo(13, 12) == 24);
        CHECK(roundUpTo(100, 12) == 108);
    }

    TEST_CASE("agrees with alignTo on power-of-two alignments")
    {
        using aegis::rhi::utility::alignTo;
        for (std::size_t size = 0; size < 40; ++size)
        {
            CHECK(roundUpTo(size, 4) == alignTo(size, 4));
            CHECK(roundUpTo(size, 16) == alignTo(size, 16));
        }
    }

    TEST_CASE("a multiple of one is the identity")
    {
        CHECK(roundUpTo(0, 1) == 0);
        CHECK(roundUpTo(1234, 1) == 1234);
    }
}
