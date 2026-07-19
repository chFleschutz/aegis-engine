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
