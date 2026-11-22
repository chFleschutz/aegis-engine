#include "glm_oracle.h"

import std;
import aegis.math;

namespace math = aegis::math;

TEST_SUITE("math::constants")
{
    TEST_CASE("pi and twoPi carry the precision of their type")
    {
        CHECK(math::pi<float> == std::numbers::pi_v<float>);
        CHECK(math::pi<double> == std::numbers::pi);
        CHECK(math::twoPi<double> == 2.0 * std::numbers::pi);
        CHECK(math::twoPi<float> == glm::two_pi<float>());
    }
}

TEST_SUITE("math::radians")
{
    TEST_CASE("converts between degrees and radians like glm")
    {
        for (const float angle : { -720.0f, -90.0f, 0.0f, 45.0f, 180.0f, 359.0f })
        {
            CAPTURE(angle);
            CHECK(math::radians(angle) == oracle::near(glm::radians(angle), 1e-6));
            CHECK(math::degrees(math::radians(angle)) == oracle::near(angle, 1e-6));
        }
        CHECK(math::radians(180.0) == std::numbers::pi);
    }
}

TEST_SUITE("math::mod")
{
    TEST_CASE("floors like glm so the result takes the sign of the divisor")
    {
        for (const float x : { -7.5f, -1.0f, 0.0f, 2.25f, 13.0f })
        {
            for (const float y : { -3.0f, 0.5f, 6.283185f })
            {
                CAPTURE(x);
                CAPTURE(y);
                CHECK(math::mod(x, y) == oracle::near(glm::mod(x, y), 1e-6));
            }
        }
        CHECK(math::mod(-1.0f, 3.0f) == 2.0f);
    }
}

TEST_SUITE("math::scalar helpers")
{
    TEST_CASE("fract stays in the unit interval for negative input")
    {
        CHECK(math::fract(2.25f) == 0.25f);
        CHECK(math::fract(-0.25f) == 0.75f);
        CHECK(math::floor(-0.25) == -1.0);
    }

    TEST_CASE("abs leaves unsigned values alone")
    {
        CHECK(math::abs(-3) == 3);
        CHECK(math::abs(3u) == 3u);
        CHECK(math::abs(-2.5) == 2.5);
    }

    TEST_CASE("clamp and saturate bound the value")
    {
        CHECK(math::clamp(5, 0, 3) == 3);
        CHECK(math::clamp(-1.0f, 0.0f, 3.0f) == 0.0f);
        CHECK(math::saturate(1.5f) == 1.0f);
        CHECK(math::saturate(0.25) == 0.25);
        CHECK(math::min(2u, 7u) == 2u);
        CHECK(math::max(-2, -7) == -2);
    }

    TEST_CASE("lerp is exact at both ends")
    {
        CHECK(math::lerp(3.0f, 7.0f, 0.0f) == 3.0f);
        CHECK(math::lerp(3.0f, 7.0f, 1.0f) == 7.0f);
        CHECK(math::lerp(3.0, 7.0, 0.25) == 4.0);
    }
}

TEST_SUITE("math::nearlyEqual")
{
    TEST_CASE("is absolute near zero and relative for large values")
    {
        CHECK(math::nearlyEqual(0.0f, 0.5e-5f));
        CHECK_FALSE(math::nearlyEqual(0.0f, 2e-5f));
        CHECK(math::nearlyEqual(1.0e6f, 1.0e6f + 5.0f));
        CHECK_FALSE(math::nearlyEqual(1.0e6f, 1.0e6f + 20.0f));
        CHECK(math::nearlyEqual(1.0, 1.0 + 1e-13));
        CHECK_FALSE(math::nearlyEqual(1.0, 1.0 + 1e-11));
        CHECK(math::nearlyEqual(1.0f, 1.1f, 0.2f));
    }
}

TEST_SUITE("math::halfToFloat")
{
    TEST_CASE("matches glm for every half value")
    {
        int mismatches = 0;
        for (std::uint32_t bits = 0; bits <= 0xFFFFu; ++bits)
        {
            const float actual = math::halfToFloat(static_cast<std::uint16_t>(bits));
            const float expected = glm::unpackHalf1x16(static_cast<glm::uint16>(bits));
            const bool same = std::isnan(expected) ? std::isnan(actual) : std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected);
            if (!same)
                ++mismatches;
        }
        CHECK(mismatches == 0);
    }

    TEST_CASE("decodes the special values")
    {
        CHECK(math::halfToFloat(0x3C00u) == 1.0f);
        CHECK(math::halfToFloat(0xC000u) == -2.0f);
        CHECK(math::halfToFloat(0x0001u) == 0x1p-24f);
        CHECK(math::halfToFloat(0x7C00u) == std::numeric_limits<float>::infinity());
        CHECK(std::isnan(math::halfToFloat(0x7E00u)));
        CHECK(std::signbit(math::halfToFloat(0x8000u)));
        static_assert(math::halfToFloat(0x3800u) == 0.5f);
    }
}
