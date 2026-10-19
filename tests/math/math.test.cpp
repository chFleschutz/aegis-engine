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

namespace
{
void checkMat4(const glm::mat4& actual, const glm::mat4& expected, float epsilon = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK(actual[c][r] == Approx(expected[c][r]).epsilon(epsilon).scale(1.0));
}
}

TEST_SUITE("aegis::math::decomposeTRS")
{
    TEST_CASE("round-trips a rotated, non-uniformly scaled transform and keeps a mirrored axis")
    {
        const glm::quat rotation = glm::angleAxis(glm::radians(30.0f), glm::normalize(glm::vec3{ 0.0f, 0.0f, 1.0f })) *
                                   glm::angleAxis(glm::radians(50.0f), glm::vec3{ 1.0f, 0.0f, 0.0f });
        const glm::vec3 location{ 1.0f, -2.0f, 3.0f };

        SUBCASE("positive scale")
        {
            const glm::mat4 m = aegis::math::tranformationMatrix(location, rotation, { 1.0f, 2.0f, 3.0f });
            glm::vec3 t, s;
            glm::quat q;
            aegis::math::decomposeTRS(m, t, q, s);
            checkVec3(t, location);
            checkVec3(s, { 1.0f, 2.0f, 3.0f });
            CHECK(glm::length(q) == Approx(1.0f));
            checkMat4(aegis::math::tranformationMatrix(t, q, s), m);
        }
        SUBCASE("mirrored x axis")
        {
            const glm::mat4 m = aegis::math::tranformationMatrix(location, rotation, { -1.0f, 2.0f, 1.0f });
            glm::vec3 t, s;
            glm::quat q;
            aegis::math::decomposeTRS(m, t, q, s);
            CHECK(glm::length(q) == Approx(1.0f));
            CHECK(s.x * s.y * s.z < 0.0f);
            checkMat4(aegis::math::tranformationMatrix(t, q, s), m);
        }
        SUBCASE("mirrored y axis")
        {
            const glm::mat4 m = aegis::math::tranformationMatrix(location, rotation, { 1.5f, -2.0f, 1.0f });
            glm::vec3 t, s;
            glm::quat q;
            aegis::math::decomposeTRS(m, t, q, s);
            checkMat4(aegis::math::tranformationMatrix(t, q, s), m);
        }
        SUBCASE("a zero scale keeps the previous rotation and produces no NaN")
        {
            const glm::mat4 m = aegis::math::tranformationMatrix(location, rotation, { 0.0f, 2.0f, 1.0f });
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

TEST_SUITE("aegis::math::viewMatrix")
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
