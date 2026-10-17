#include <doctest/doctest.h>

import std;
import aegis.rhi;

TEST_SUITE("rhi::resolveAnisotropy")
{
    using aegis::rhi::detail::resolveAnisotropy;

    TEST_CASE("a request of 1 or less disables anisotropic filtering")
    {
        CHECK(resolveAnisotropy(1.0f, 16.0f) == std::nullopt);
        CHECK(resolveAnisotropy(0.5f, 16.0f) == std::nullopt);
        CHECK(resolveAnisotropy(0.0f, 16.0f) == std::nullopt);
    }

    TEST_CASE("a request within the device limit is used as-is")
    {
        CHECK(resolveAnisotropy(4.0f, 16.0f) == 4.0f);
        CHECK(resolveAnisotropy(16.0f, 16.0f) == 16.0f);
    }

    TEST_CASE("a request above the device limit is clamped to it")
    {
        CHECK(resolveAnisotropy(16.0f, 8.0f) == 8.0f);
    }

    TEST_CASE("a device limit of 1 or less disables anisotropic filtering")
    {
        CHECK(resolveAnisotropy(16.0f, 1.0f) == std::nullopt);
    }
}
