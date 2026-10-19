#include <doctest/doctest.h>

import std;
import aegis.rhi;

TEST_SUITE("rhi::formatName")
{
    TEST_CASE("names a format after its enumerator")
    {
        CHECK(aegis::rhi::formatName(aegis::rhi::Format::RGBA16_SFLOAT) == std::string_view{ "RGBA16_SFLOAT" });
        CHECK(aegis::rhi::formatName(aegis::rhi::Format::D32_SFLOAT) == std::string_view{ "D32_SFLOAT" });
        CHECK(aegis::rhi::formatName(aegis::rhi::Format::Unknown) == std::string_view{ "Unknown" });
    }
}
