#include <doctest/doctest.h>

import std;
import aegis.math;

namespace
{
using doctest::Approx;

void checkVec3(const glm::vec3& actual, const glm::vec3& expected)
{
    CHECK(actual.x == Approx(expected.x));
    CHECK(actual.y == Approx(expected.y));
    CHECK(actual.z == Approx(expected.z));
}
}

TEST_SUITE("math::basisVectors")
{
    TEST_CASE("an identity quaternion yields the world axes")
    {
        const glm::quat identity{ 1.0f, 0.0f, 0.0f, 0.0f };
        checkVec3(aegis::math::forward(identity), aegis::math::world::FORWARD);
        checkVec3(aegis::math::right(identity), aegis::math::world::RIGHT);
        checkVec3(aegis::math::up(identity), aegis::math::world::UP);
    }
}

namespace
{
void checkMat4(const glm::mat4& actual, const glm::mat4& expected, float epsilon = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK(aegis::math::element(actual, r, c) == Approx(aegis::math::element(expected, r, c)).epsilon(epsilon).scale(1.0));
}
}

TEST_SUITE("math::decomposeTRS")
{
    TEST_CASE("round-trips a rotated, non-uniformly scaled transform and keeps a mirrored axis")
    {
        const glm::quat rotation = glm::angleAxis(glm::radians(30.0f), glm::normalize(glm::vec3{ 0.0f, 0.0f, 1.0f })) *
                                   glm::angleAxis(glm::radians(50.0f), glm::vec3{ 1.0f, 0.0f, 0.0f });
        const glm::vec3 location{ 1.0f, -2.0f, 3.0f };

        SUBCASE("positive scale")
        {
            const glm::mat4 m = aegis::math::transformMatrix(location, rotation, { 1.0f, 2.0f, 3.0f });
            glm::vec3 t, s;
            glm::quat q{ 1.0f, 0.0f, 0.0f, 0.0f };
            aegis::math::decomposeTRS(m, t, q, s);
            checkVec3(t, location);
            checkVec3(s, { 1.0f, 2.0f, 3.0f });
            CHECK(glm::length(q) == Approx(1.0f));
            checkMat4(aegis::math::transformMatrix(t, q, s), m);
        }
        SUBCASE("mirrored x axis")
        {
            const glm::mat4 m = aegis::math::transformMatrix(location, rotation, { -1.0f, 2.0f, 1.0f });
            glm::vec3 t, s;
            glm::quat q{ 1.0f, 0.0f, 0.0f, 0.0f };
            aegis::math::decomposeTRS(m, t, q, s);
            CHECK(glm::length(q) == Approx(1.0f));
            CHECK(s.x * s.y * s.z < 0.0f);
            checkMat4(aegis::math::transformMatrix(t, q, s), m);
        }
        SUBCASE("mirrored y axis")
        {
            const glm::mat4 m = aegis::math::transformMatrix(location, rotation, { 1.5f, -2.0f, 1.0f });
            glm::vec3 t, s;
            glm::quat q{ 1.0f, 0.0f, 0.0f, 0.0f };
            aegis::math::decomposeTRS(m, t, q, s);
            checkMat4(aegis::math::transformMatrix(t, q, s), m);
        }
        SUBCASE("a zero scale keeps the previous rotation and produces no NaN")
        {
            const glm::mat4 m = aegis::math::transformMatrix(location, rotation, { 0.0f, 2.0f, 1.0f });
            glm::vec3 t, s;
            glm::quat q = rotation;
            aegis::math::decomposeTRS(m, t, q, s);
            CHECK(q.w == rotation.w);
            CHECK(q.x == rotation.x);
            CHECK(q.y == rotation.y);
            CHECK(q.z == rotation.z);
            CHECK(s.x == 0.0f);
        }
    }
}

TEST_SUITE("math::viewMatrix")
{
    TEST_CASE("inverse view times view is the identity")
    {
        const glm::vec3 location{ 3.0f, -4.0f, 2.5f };
        const glm::vec3 forward{ 0.3f, 1.0f, -0.2f };
        const glm::mat4 view = aegis::math::viewMatrix(location, forward);
        const glm::mat4 inverseView = aegis::math::inverseViewMatrix(location, forward);
        checkMat4(view * inverseView, glm::mat4{ 1.0f });
        checkMat4(inverseView * view, glm::mat4{ 1.0f });
    }
}

// The bridges on glm must give what the new library gives, so that moving the backend changes no value.
namespace
{
void checkVec4(const glm::vec4& actual, const glm::vec4& expected)
{
    CHECK(actual.x == expected.x);
    CHECK(actual.y == expected.y);
    CHECK(actual.z == expected.z);
    CHECK(actual.w == expected.w);
}

template<typename G, typename M>
void checkSameMatrix(const G& bridge, const M& expected, int rows, int columns)
{
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < columns; ++c)
        {
            CAPTURE(r);
            CAPTURE(c);
            CHECK(aegis::math::element(bridge, r, c) == expected[static_cast<std::size_t>(r), static_cast<std::size_t>(c)]);
        }
    }
}
}

TEST_SUITE("math bridges")
{
    using namespace aegis;

    TEST_CASE("fromRows, fromColumns, row and column read the matrix as the new types do")
    {
        const glm::vec4 a{ 1.0f, 2.0f, 3.0f, 4.0f };
        const glm::vec4 b{ 5.0f, 6.0f, 7.0f, 8.0f };
        const glm::vec4 c{ 9.0f, 10.0f, 11.0f, 12.0f };
        const glm::vec4 d{ 13.0f, 14.0f, 15.0f, 16.0f };
        const math::Float4 na{ 1.0f, 2.0f, 3.0f, 4.0f };
        const math::Float4 nb{ 5.0f, 6.0f, 7.0f, 8.0f };
        const math::Float4 nc{ 9.0f, 10.0f, 11.0f, 12.0f };
        const math::Float4 nd{ 13.0f, 14.0f, 15.0f, 16.0f };

        const auto rows = math::fromRows(a, b, c, d);
        checkSameMatrix(rows, math::Float4x4::fromRows(na, nb, nc, nd), 4, 4);
        checkSameMatrix(math::fromColumns(a, b, c, d), math::Float4x4::fromColumns(na, nb, nc, nd), 4, 4);
        checkSameMatrix(math::fromRows(a, b, c), math::Float3x4::fromRows(na, nb, nc), 3, 4);
        checkSameMatrix(math::fromRows(glm::vec3{ a }, glm::vec3{ b }, glm::vec3{ c }),
            math::Float3x3::fromRows(math::Float3{ na }, math::Float3{ nb }, math::Float3{ nc }), 3, 3);
        checkSameMatrix(math::fromColumns(glm::vec3{ a }, glm::vec3{ b }, glm::vec3{ c }),
            math::Float3x3::fromColumns(math::Float3{ na }, math::Float3{ nb }, math::Float3{ nc }), 3, 3);

        checkVec4(math::row(rows, 1), b);
        checkVec4(math::column(rows, 1), glm::vec4{ 2.0f, 6.0f, 10.0f, 14.0f });
        checkVec4(math::row(math::fromRows(a, b, c), 2), c);
        checkVec3(math::row(glm::mat3{ rows }, 1), glm::vec3{ 5.0f, 6.0f, 7.0f });
        checkVec3(math::column(glm::mat3{ rows }, 1), glm::vec3{ 2.0f, 6.0f, 10.0f });
    }

    TEST_CASE("perspective and orthographic are the new library's builders")
    {
        checkSameMatrix(math::perspective(1.2f, 1.6f, 0.1f, 500.0f), math::perspective<float>(1.2f, 1.6f, 0.1f, 500.0f), 4, 4);
        checkSameMatrix(math::orthographic(12.5f, 7.0f, 0.5f, 80.0f), math::orthographic<float>(12.5f, 7.0f, 0.5f, 80.0f), 4, 4);
    }

    TEST_CASE("quatFromEuler and quatFromXyzw are Quat::fromEuler and Quat::fromXyzw")
    {
        const glm::vec3 radians{ 0.3f, -1.1f, 2.4f };
        const auto bridge = math::quatFromEuler(radians);
        const auto expected = math::Quat::fromEuler(math::Float3{ radians.x, radians.y, radians.z });
        CHECK(bridge.x == Approx(expected.x));
        CHECK(bridge.y == Approx(expected.y));
        CHECK(bridge.z == Approx(expected.z));
        CHECK(bridge.w == Approx(expected.w));

        const auto q = math::quatFromXyzw(0.1f, 0.2f, 0.3f, 0.4f);
        CHECK(q.x == 0.1f);
        CHECK(q.y == 0.2f);
        CHECK(q.z == 0.3f);
        CHECK(q.w == 0.4f);
    }
}
