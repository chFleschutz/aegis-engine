#include "glm_oracle.h"

import std;
import aegis.math;

namespace math = aegis::math;
using math::Float3;
using math::Float3x4;
using math::Float4;
using math::Float4x4;

TEST_SUITE("math::layout")
{
    TEST_CASE("a 3x4 matrix is the three rows the shaders read")
    {
        // The 48-byte instance transform: the shaders are compiled row-major (-matrix-layout-row-major).
        const auto m = Float3x4::fromRows(
            Float4{ 1.0f, 2.0f, 3.0f, 4.0f },
            Float4{ 5.0f, 6.0f, 7.0f, 8.0f },
            Float4{ 9.0f, 10.0f, 11.0f, 12.0f });
        const auto bytes = std::bit_cast<std::array<float, 12>>(m);
        CHECK(bytes == std::array{ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f });
    }

    TEST_CASE("a 4x4 matrix holds the bytes of glm's transpose")
    {
        // What glm::rowMajor4 produces today, so uploads can drop the conversion.
        auto m = Float4x4::identity();
        m.setColumn(3, Float4{ 1.0f, 2.0f, 3.0f, 1.0f });
        m[0, 1] = 0.5f;
        const glm::mat<4, 4, float> rowMajor = glm::transpose(oracle::toGlmMat(m));
        CHECK(std::bit_cast<std::array<float, 16>>(m) == std::bit_cast<std::array<float, 16>>(rowMajor));
    }

    TEST_CASE("vectors and quaternions are their components in order")
    {
        CHECK(std::bit_cast<std::array<float, 3>>(Float3{ 1.0f, 2.0f, 3.0f }) == std::array{ 1.0f, 2.0f, 3.0f });
        CHECK(std::bit_cast<std::array<std::uint32_t, 2>>(math::Uint2{ 7u, 9u }) == std::array<std::uint32_t, 2>{ 7u, 9u });
        CHECK(std::bit_cast<std::array<float, 4>>(math::Quat::fromXyzw(1.0f, 2.0f, 3.0f, 4.0f)) == std::array{ 1.0f, 2.0f, 3.0f, 4.0f });
    }
}
