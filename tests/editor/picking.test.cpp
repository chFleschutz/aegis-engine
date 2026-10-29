#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;
import aegis.world;

namespace
{
using aegis::StableId;
using aegis::editor::PickIcon;
using aegis::editor::PickTarget;
using aegis::editor::Ray;
using aegis::editor::Sphere;

constexpr StableId a{ 1 };
constexpr StableId b{ 2 };

// From the origin along +Y
constexpr Ray alongY{ .origin = { 0.0f, 0.0f, 0.0f }, .direction = { 0.0f, 1.0f, 0.0f } };
}

TEST_SUITE("editor::intersect")
{
    TEST_CASE("returns the distance to the sphere's near side")
    {
        const auto distance = intersect(alongY, Sphere{ .center = { 0.0f, 5.0f, 0.0f }, .radius = 1.0f });
        REQUIRE(distance);
        CHECK(*distance == doctest::Approx(4.0f));
    }

    TEST_CASE("misses a sphere beside the ray")
    {
        CHECK_FALSE(intersect(alongY, Sphere{ .center = { 2.0f, 5.0f, 0.0f }, .radius = 1.0f }));
    }

    TEST_CASE("misses a sphere behind the origin")
    {
        CHECK_FALSE(intersect(alongY, Sphere{ .center = { 0.0f, -5.0f, 0.0f }, .radius = 1.0f }));
    }

    TEST_CASE("returns 0 from inside the sphere")
    {
        const auto distance = intersect(alongY, Sphere{ .center = { 0.0f, 0.5f, 0.0f }, .radius = 1.0f });
        REQUIRE(distance);
        CHECK(*distance == doctest::Approx(0.0f));
    }

    TEST_CASE("hits a sphere the ray only grazes")
    {
        CHECK(intersect(alongY, Sphere{ .center = { 1.0f, 5.0f, 0.0f }, .radius = 1.0f }));
    }
}

TEST_SUITE("editor::pickNearest")
{
    TEST_CASE("picks the sphere the ray enters first")
    {
        const std::array targets{
            PickTarget{ .id = a, .bounds = { .center = { 0.0f, 10.0f, 0.0f }, .radius = 1.0f } },
            PickTarget{ .id = b, .bounds = { .center = { 0.0f, 5.0f, 0.0f }, .radius = 1.0f } },
        };
        CHECK(pickNearest(alongY, targets) == b);
    }

    TEST_CASE("a large sphere behind a small one can be nearer")
    {
        const std::array targets{
            PickTarget{ .id = a, .bounds = { .center = { 0.0f, 5.0f, 0.0f }, .radius = 0.5f } },
            PickTarget{ .id = b, .bounds = { .center = { 0.0f, 8.0f, 0.0f }, .radius = 4.0f } },
        };
        CHECK(pickNearest(alongY, targets) == b);
    }

    TEST_CASE("picks nothing when the ray misses every sphere")
    {
        const std::array targets{
            PickTarget{ .id = a, .bounds = { .center = { 3.0f, 5.0f, 0.0f }, .radius = 1.0f } },
        };
        CHECK_FALSE(pickNearest(alongY, targets));
        CHECK_FALSE(pickNearest(alongY, {}));
    }
}

TEST_SUITE("editor::pickIcon")
{
    TEST_CASE("picks the icon closest to the cursor")
    {
        const std::array icons{
            PickIcon{ .id = a, .position = { 100.0f, 100.0f }, .radius = 10.0f },
            PickIcon{ .id = b, .position = { 106.0f, 100.0f }, .radius = 10.0f },
        };
        CHECK(pickIcon({ 104.0f, 100.0f }, icons) == b);
    }

    TEST_CASE("picks nothing beyond the radius")
    {
        const std::array icons{ PickIcon{ .id = a, .position = { 100.0f, 100.0f }, .radius = 10.0f } };
        CHECK_FALSE(pickIcon({ 120.0f, 100.0f }, icons));
        CHECK_FALSE(aegis::editor::pickIcon({ 100.0f, 100.0f }, {}));
    }

    TEST_CASE("measures each icon by its own radius")
    {
        const std::array icons{
            PickIcon{ .id = a, .position = { 100.0f, 100.0f }, .radius = 4.0f },
            PickIcon{ .id = b, .position = { 112.0f, 100.0f }, .radius = 10.0f },
        };
        CHECK(pickIcon({ 105.0f, 100.0f }, icons) == b); // Nearer a, but outside its small radius
        CHECK(pickIcon({ 103.0f, 100.0f }, icons) == a);
        CHECK_FALSE(pickIcon({ 100.0f, 106.0f }, icons));
    }

    TEST_CASE("picks the nearer center where two icons overlap")
    {
        const std::array icons{
            PickIcon{ .id = a, .position = { 100.0f, 100.0f }, .radius = 20.0f },
            PickIcon{ .id = b, .position = { 108.0f, 100.0f }, .radius = 4.0f },
        };
        CHECK(pickIcon({ 107.0f, 100.0f }, icons) == b);
        CHECK(pickIcon({ 103.0f, 100.0f }, icons) == a);
    }
}

TEST_SUITE("editor::transformSphere")
{
    TEST_CASE("moves the center and scales the radius by the largest axis")
    {
        const glm::mat4 transform = aegis::math::transformMatrix(glm::vec3{ 1.0f, 2.0f, 3.0f }, glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, glm::vec3{ 1.0f, 3.0f, 2.0f });
        const Sphere sphere = transformSphere(Sphere{ .center = { 1.0f, 1.0f, 0.0f }, .radius = 2.0f }, transform);
        CHECK(sphere.center.x == doctest::Approx(2.0f));
        CHECK(sphere.center.y == doctest::Approx(5.0f));
        CHECK(sphere.center.z == doctest::Approx(3.0f));
        CHECK(sphere.radius == doctest::Approx(6.0f));
    }

    TEST_CASE("keeps the radius under a rotation")
    {
        const glm::quat rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{ 0.0f, 0.0f, 1.0f });
        const glm::mat4 transform = aegis::math::transformMatrix(glm::vec3{ 0.0f }, rotation, glm::vec3{ 1.0f });
        const Sphere sphere = transformSphere(Sphere{ .center = { 1.0f, 0.0f, 0.0f }, .radius = 1.5f }, transform);
        CHECK(sphere.center.y == doctest::Approx(1.0f));
        CHECK(sphere.radius == doctest::Approx(1.5f));
    }
}
