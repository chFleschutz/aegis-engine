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
    TEST_CASE("an identity quaternion yields the world axes")
    {
        const glm::quat identity{ 1.0f, 0.0f, 0.0f, 0.0f };
        checkVec3(aegis::math::forward(identity), aegis::math::world::FORWARD);
        checkVec3(aegis::math::right(identity), aegis::math::world::RIGHT);
        checkVec3(aegis::math::up(identity), aegis::math::world::UP);
    }
}
