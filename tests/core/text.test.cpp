#include <doctest/doctest.h>

import std;
import aegis.core;

using aegis::core::containsIgnoringCase;

TEST_SUITE("core::containsIgnoringCase")
{
    TEST_CASE("finds the part in any case")
    {
        CHECK(containsIgnoringCase("Sponza_Floor", "floor"));
        CHECK(containsIgnoringCase("Sponza_Floor", "SPONZA"));
        CHECK(containsIgnoringCase("Sponza_Floor", "a_F"));
    }

    TEST_CASE("misses a part that is not there")
    {
        CHECK_FALSE(containsIgnoringCase("Sponza", "sponzas"));
        CHECK_FALSE(containsIgnoringCase("", "a"));
    }

    TEST_CASE("every text holds the empty part")
    {
        CHECK(containsIgnoringCase("Sponza", ""));
        CHECK(containsIgnoringCase("", ""));
    }
}
