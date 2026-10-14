#include <doctest/doctest.h>

import Aegis.Math;

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

TEST_SUITE("Aegis::Math basis vectors")
{
    TEST_CASE("an unrotated Euler rotation yields the world axes")
    {
        const glm::vec3 noRotation{ 0.0f, 0.0f, 0.0f };
        checkVec3(Aegis::Math::forward(noRotation), Aegis::Math::World::FORWARD);
        checkVec3(Aegis::Math::right(noRotation), Aegis::Math::World::RIGHT);
        checkVec3(Aegis::Math::up(noRotation), Aegis::Math::World::UP);
    }

    TEST_CASE("an identity quaternion yields the world axes")
    {
        const glm::quat identity{ 1.0f, 0.0f, 0.0f, 0.0f };
        checkVec3(Aegis::Math::forward(identity), Aegis::Math::World::FORWARD);
        checkVec3(Aegis::Math::right(identity), Aegis::Math::World::RIGHT);
        checkVec3(Aegis::Math::up(identity), Aegis::Math::World::UP);
    }

    TEST_CASE("the basis vectors stay mutually orthonormal after a rotation")
    {
        const glm::vec3 rotation{ 0.3f, -0.7f, 1.1f };
        const glm::vec3 f = Aegis::Math::forward(rotation);
        const glm::vec3 r = Aegis::Math::right(rotation);
        const glm::vec3 u = Aegis::Math::up(rotation);

        CHECK(glm::length(f) == Approx(1.0f));
        CHECK(glm::length(r) == Approx(1.0f));
        CHECK(glm::length(u) == Approx(1.0f));

        CHECK(glm::dot(f, r) == Approx(0.0f));
        CHECK(glm::dot(f, u) == Approx(0.0f));
        CHECK(glm::dot(r, u) == Approx(0.0f));
    }
}

TEST_SUITE("Aegis::Math::inFOV")
{
    TEST_CASE("a target dead ahead is inside any positive field of view")
    {
        const glm::vec3 view{ 0.0f, 1.0f, 0.0f };
        CHECK(Aegis::Math::inFOV(view, view, glm::radians(90.0f)));
    }

    TEST_CASE("a target behind the viewer is outside a narrow field of view")
    {
        const glm::vec3 view{ 0.0f, 1.0f, 0.0f };
        const glm::vec3 behind{ 0.0f, -1.0f, 0.0f };
        CHECK_FALSE(Aegis::Math::inFOV(view, behind, glm::radians(90.0f)));
    }
}
