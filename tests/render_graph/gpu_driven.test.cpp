#include <doctest/doctest.h>

import std;
import aegis.render_graph;

TEST_SUITE("render_graph::detail::grownCapacity")
{
    using aegis::render_graph::detail::grownCapacity;

    TEST_CASE("keeps the capacity when the required count fits")
    {
        CHECK(grownCapacity(65536, 0) == 65536);
        CHECK(grownCapacity(65536, 1000) == 65536);
        CHECK(grownCapacity(65536, 65536) == 65536);
    }

    TEST_CASE("grows to the next power of two that fits")
    {
        CHECK(grownCapacity(65536, 65537) == 131072);
        CHECK(grownCapacity(65536, 1'000'001) == 1'048'576);
        CHECK(grownCapacity(4096, 8192) == 8192);
    }

    TEST_CASE("never shrinks")
    {
        CHECK(grownCapacity(1'048'576, 10) == 1'048'576);
    }

    TEST_CASE("grows to the exact count beyond the largest 32-bit power of two")
    {
        constexpr std::uint32_t huge{ (1u << 31) + 1 };
        CHECK(grownCapacity(65536, huge) == huge);
    }
}
