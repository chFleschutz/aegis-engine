#include <doctest/doctest.h>

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

TEST_SUITE("aegis::math basis vectors")
{
    TEST_CASE("an unrotated Euler rotation yields the world axes")
    {
        const glm::vec3 noRotation{ 0.0f, 0.0f, 0.0f };
        checkVec3(aegis::math::forward(noRotation), aegis::math::world::FORWARD);
        checkVec3(aegis::math::right(noRotation), aegis::math::world::RIGHT);
        checkVec3(aegis::math::up(noRotation), aegis::math::world::UP);
    }

    TEST_CASE("an identity quaternion yields the world axes")
    {
        const glm::quat identity{ 1.0f, 0.0f, 0.0f, 0.0f };
        checkVec3(aegis::math::forward(identity), aegis::math::world::FORWARD);
        checkVec3(aegis::math::right(identity), aegis::math::world::RIGHT);
        checkVec3(aegis::math::up(identity), aegis::math::world::UP);
    }

    TEST_CASE("the basis vectors stay mutually orthonormal after a rotation")
    {
        const glm::vec3 rotation{ 0.3f, -0.7f, 1.1f };
        const glm::vec3 f = aegis::math::forward(rotation);
        const glm::vec3 r = aegis::math::right(rotation);
        const glm::vec3 u = aegis::math::up(rotation);

        CHECK(glm::length(f) == Approx(1.0f));
        CHECK(glm::length(r) == Approx(1.0f));
        CHECK(glm::length(u) == Approx(1.0f));

        CHECK(glm::dot(f, r) == Approx(0.0f));
        CHECK(glm::dot(f, u) == Approx(0.0f));
        CHECK(glm::dot(r, u) == Approx(0.0f));
    }
}

TEST_SUITE("aegis::math::inFOV")
{
    TEST_CASE("a target dead ahead is inside any positive field of view")
    {
        const glm::vec3 view{ 0.0f, 1.0f, 0.0f };
        CHECK(aegis::math::inFOV(view, view, glm::radians(90.0f)));
    }

    TEST_CASE("a target behind the viewer is outside a narrow field of view")
    {
        const glm::vec3 view{ 0.0f, 1.0f, 0.0f };
        const glm::vec3 behind{ 0.0f, -1.0f, 0.0f };
        CHECK_FALSE(aegis::math::inFOV(view, behind, glm::radians(90.0f)));
    }
}
