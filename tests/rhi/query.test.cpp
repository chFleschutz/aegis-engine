#include <doctest/doctest.h>

import std;
import aegis.rhi;

TEST_SUITE("rhi::timestampDeltaMs")
{
    using aegis::rhi::detail::timestampDeltaMs;

    TEST_CASE("a delta converts ticks to milliseconds by the timestamp period")
    {
        CHECK(timestampDeltaMs(1'000, 2'001'000, 1.0f, 64) == doctest::Approx(2.0));
        CHECK(timestampDeltaMs(0, 1'000'000, 52.08f, 64) == doctest::Approx(52.08));
    }

    TEST_CASE("equal timestamps measure zero")
    {
        CHECK(timestampDeltaMs(42, 42, 1.0f, 64) == 0.0);
    }

    TEST_CASE("a counter that wrapped at its valid bit width still yields the forward delta")
    {
        constexpr std::uint64_t wrap{ std::uint64_t{ 1 } << 36 };
        // 10 ticks up to the wrap, 5 after it.
        CHECK(timestampDeltaMs(wrap - 10, 5, 1'000'000.0f, 36) == doctest::Approx(15.0));
    }

    TEST_CASE("a 64-bit counter wraps at the full width")
    {
        constexpr auto max = std::numeric_limits<std::uint64_t>::max();
        CHECK(timestampDeltaMs(max - 1, 1, 1'000'000.0f, 64) == doctest::Approx(3.0));
    }
}
