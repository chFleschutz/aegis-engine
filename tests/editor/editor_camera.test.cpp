#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;

namespace
{
using aegis::editor::CameraInput;
using aegis::editor::EditorCamera;

auto near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f) -> bool
{
    return glm::length(a - b) <= epsilon;
}

auto near(const glm::vec2& a, const glm::vec2& b, float epsilon = 1e-4f) -> bool
{
    return glm::length(a - b) <= epsilon;
}

// At the origin, looking along +Y with the pivot 10 ahead
auto originCamera() -> EditorCamera
{
    EditorCamera camera;
    camera.lookAlong(glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, 10.0f);
    return camera;
}
}

TEST_SUITE("editor::EditorCamera")
{
    TEST_CASE("lookAlong sets the view direction and the pivot")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 1.0f, 2.0f, 3.0f }, glm::vec3{ 1.0f, 0.0f, 0.0f }, 4.0f);
        CHECK(near(camera.forward(), glm::vec3{ 1.0f, 0.0f, 0.0f }));
        CHECK(near(camera.pivot(), glm::vec3{ 5.0f, 2.0f, 3.0f }));
        CHECK(near(camera.right(), glm::vec3{ 0.0f, -1.0f, 0.0f }));
        CHECK(near(camera.up(), glm::vec3{ 0.0f, 0.0f, 1.0f }));
    }

    TEST_CASE("lookAlong keeps the pitch short of straight down")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 0.0f, -1.0f });
        CHECK(camera.pitch() > glm::radians(-90.0f));
        CHECK(glm::length(camera.right()) == doctest::Approx(1.0f));
    }

    TEST_CASE("the view matrix moves the camera to the origin looking along -Z")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 2.0f, 3.0f, 4.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f });
        const glm::mat4 view = camera.view();
        CHECK(near(glm::vec3{ view * glm::vec4{ camera.position(), 1.0f } }, glm::vec3{ 0.0f }));
        CHECK(near(glm::vec3{ view * glm::vec4{ camera.forward(), 0.0f } }, glm::vec3{ 0.0f, 0.0f, -1.0f }));
    }

    TEST_CASE("the projection flips y and maps near and far to depth 0 and 1")
    {
        const EditorCamera camera = originCamera();
        const glm::mat4 projection = camera.projection(2.0f);
        CHECK(projection[1][1] < 0.0f);
        const auto depth = [&](float distance) {
            const glm::vec4 clip = projection * glm::vec4{ 0.0f, 0.0f, -distance, 1.0f };
            return clip.z / clip.w;
        };
        CHECK(depth(camera.near) == doctest::Approx(0.0f));
        CHECK(depth(camera.far) == doctest::Approx(1.0f));
    }

    TEST_CASE("flying moves along the view at the speed")
    {
        EditorCamera camera = originCamera();
        camera.setSpeed(2.0f);
        camera.update({ .move = { 0.0f, 1.0f, 0.0f }, .fly = true }, 0.5f);
        CHECK(near(camera.position(), glm::vec3{ 0.0f, 1.0f, 0.0f }));
        CHECK(camera.distance() == doctest::Approx(10.0f));
    }

    TEST_CASE("flying moves only while RMB is held")
    {
        EditorCamera camera = originCamera();
        camera.update({ .move = { 1.0f, 1.0f, 1.0f } }, 1.0f);
        CHECK(near(camera.position(), glm::vec3{ 0.0f }));
    }

    TEST_CASE("flying diagonally is no faster than straight")
    {
        EditorCamera camera = originCamera();
        camera.setSpeed(1.0f);
        camera.update({ .move = { 1.0f, 1.0f, 0.0f }, .fly = true }, 1.0f);
        CHECK(glm::length(camera.position()) == doctest::Approx(1.0f));
    }

    TEST_CASE("flying up and down follows the world's Z")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, -1.0f });
        camera.setSpeed(1.0f);
        camera.update({ .move = { 0.0f, 0.0f, 1.0f }, .fly = true }, 1.0f);
        CHECK(near(camera.position(), glm::vec3{ 0.0f, 0.0f, 1.0f }));
    }

    TEST_CASE("moving the cursor right while flying turns right")
    {
        EditorCamera camera = originCamera();
        camera.update({ .cursorDelta = { 100.0f, 0.0f }, .fly = true }, 0.0f);
        CHECK(camera.forward().x > 0.0f);
        CHECK(near(camera.position(), glm::vec3{ 0.0f }));
    }

    TEST_CASE("moving the cursor down while flying looks down")
    {
        EditorCamera camera = originCamera();
        camera.update({ .cursorDelta = { 0.0f, 100.0f }, .fly = true }, 0.0f);
        CHECK(camera.forward().z < 0.0f);
    }

    TEST_CASE("the wheel while flying changes the speed, not the position")
    {
        EditorCamera camera = originCamera();
        camera.setSpeed(1.0f);
        camera.update({ .wheel = 1.0f, .fly = true }, 0.0f);
        CHECK(camera.speed() > 1.0f);
        camera.update({ .wheel = -1.0f, .fly = true }, 0.0f);
        CHECK(camera.speed() == doctest::Approx(1.0f));
        CHECK(near(camera.position(), glm::vec3{ 0.0f }));
    }

    TEST_CASE("orbiting keeps the pivot and the distance")
    {
        EditorCamera camera = originCamera();
        const glm::vec3 pivot = camera.pivot();
        camera.update({ .cursorDelta = { 200.0f, -50.0f }, .orbit = true }, 0.0f);
        CHECK(near(camera.pivot(), pivot));
        CHECK(glm::length(camera.position() - pivot) == doctest::Approx(10.0f));
        CHECK_FALSE(near(camera.position(), glm::vec3{ 0.0f }));
    }

    TEST_CASE("panning moves the camera and the pivot in the view plane")
    {
        EditorCamera camera = originCamera();
        const glm::vec3 forward = camera.forward();
        camera.update({ .cursorDelta = { 10.0f, 10.0f }, .pan = true }, 0.0f);
        CHECK(near(camera.forward(), forward));
        CHECK(glm::dot(camera.position(), forward) == doctest::Approx(0.0f));
        CHECK(camera.position().x < 0.0f); // The scene follows the cursor to the right
        CHECK(camera.position().z > 0.0f); // and down
    }

    TEST_CASE("the wheel dollies toward the pivot")
    {
        EditorCamera camera = originCamera();
        const glm::vec3 pivot = camera.pivot();
        camera.update({ .wheel = 2.0f }, 0.0f);
        CHECK(camera.distance() < 10.0f);
        CHECK(near(camera.pivot(), pivot));
        camera.update({ .wheel = 1000.0f }, 0.0f);
        CHECK(camera.distance() == doctest::Approx(EditorCamera::minDistance));
    }

    TEST_CASE("focus frames the sphere along the view direction")
    {
        EditorCamera camera = originCamera();
        const glm::vec3 center{ 5.0f, 5.0f, 5.0f };
        camera.focus(center, 2.0f);
        CHECK(near(camera.pivot(), center));
        CHECK(near(camera.forward(), glm::vec3{ 0.0f, 1.0f, 0.0f }));
        // The sphere fits in the vertical field of view
        CHECK(2.0f / camera.distance() < std::sin(camera.fov / 2.0f));
    }

    TEST_CASE("focus on a point keeps a minimum distance")
    {
        EditorCamera camera = originCamera();
        camera.focus(glm::vec3{ 0.0f }, 0.0f);
        CHECK(camera.distance() >= EditorCamera::minDistance);
    }

    TEST_CASE("orbitToward keeps the pivot and the distance")
    {
        EditorCamera camera = originCamera();
        const glm::vec3 pivot = camera.pivot();
        camera.orbitToward(glm::vec3{ -1.0f, 0.0f, 0.0f });
        CHECK(near(camera.forward(), glm::vec3{ -1.0f, 0.0f, 0.0f }));
        CHECK(near(camera.pivot(), pivot));
        CHECK(camera.distance() == doctest::Approx(10.0f));
        CHECK(near(camera.position(), pivot + glm::vec3{ 10.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("orbitToward straight down keeps the yaw and stops short of vertical")
    {
        EditorCamera camera = originCamera();
        camera.orbitToward(glm::vec3{ 1.0f, 0.0f, 0.0f });
        const float yaw = camera.yaw();
        const glm::vec3 pivot = camera.pivot();
        camera.orbitToward(glm::vec3{ 0.0f, 0.0f, -1.0f });
        CHECK(camera.yaw() == doctest::Approx(yaw));
        CHECK(camera.pitch() > -glm::radians(90.0f));
        CHECK(camera.forward().z < -0.99f);
        CHECK(near(camera.pivot(), pivot));
    }

    TEST_CASE("the center ray runs along the view direction")
    {
        const EditorCamera camera = originCamera();
        const auto ray = camera.ray({ 0.0f, 0.0f }, 1.5f);
        CHECK(near(ray.origin, camera.position()));
        CHECK(near(ray.direction, camera.forward()));
    }

    TEST_CASE("a ray through a corner hits what projects there")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 1.0f, -2.0f, 3.0f }, glm::vec3{ 0.3f, 1.0f, -0.2f });
        const float aspect = 16.0f / 9.0f;
        const glm::vec2 ndc{ 0.8f, -0.6f };
        const auto ray = camera.ray(ndc, aspect);
        const auto projected = camera.project(ray.origin + ray.direction * 7.0f, aspect);
        REQUIRE(projected);
        CHECK(near(*projected, ndc));
    }

    TEST_CASE("project agrees with the projection matrix")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 1.0f, -2.0f, 3.0f }, glm::vec3{ 0.3f, 1.0f, -0.2f });
        const float aspect = 1.25f;
        const glm::vec3 point{ 2.0f, 6.0f, 1.0f };
        const glm::vec4 clip = camera.projection(aspect) * camera.view() * glm::vec4{ point, 1.0f };
        const auto projected = camera.project(point, aspect);
        REQUIRE(projected);
        // The projection flips y for Vulkan; ndc here has y up
        CHECK(near(*projected, glm::vec2{ clip.x / clip.w, -clip.y / clip.w }));
    }

    TEST_CASE("project gives none behind the camera")
    {
        const EditorCamera camera = originCamera();
        CHECK_FALSE(camera.project(glm::vec3{ 0.0f, -1.0f, 0.0f }, 1.0f));
    }

    TEST_CASE("projectSphere spans a centered sphere symmetrically")
    {
        const EditorCamera camera = originCamera();
        const float aspect = 2.0f;
        const auto rect = camera.projectSphere({ .center = { 0.0f, 10.0f, 0.0f }, .radius = 1.0f }, aspect);
        REQUIRE(rect);
        // The touching lines leave the eye at asin(radius / distance) from the axis
        const float slope = std::tan(std::asin(0.1f));
        const float tanHalfFov = std::tan(camera.fov / 2.0f);
        CHECK(near(rect->max, glm::vec2{ slope / (tanHalfFov * aspect), slope / tanHalfFov }));
        CHECK(near(rect->min, -rect->max));
    }

    TEST_CASE("projectSphere holds every point of the sphere and touches each edge")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 1.0f, -2.0f, 3.0f }, glm::vec3{ 0.3f, 1.0f, -0.2f });
        const float aspect = 16.0f / 9.0f;
        const aegis::editor::Sphere sphere{ .center = { 4.0f, 7.0f, 1.0f }, .radius = 2.0f };
        const auto rect = camera.projectSphere(sphere, aspect);
        REQUIRE(rect);

        glm::vec2 lowest{ std::numeric_limits<float>::max() };
        glm::vec2 highest{ std::numeric_limits<float>::lowest() };
        constexpr int steps = 256;
        for (int i = 0; i <= steps; ++i)
        {
            const float polar = std::numbers::pi_v<float> * static_cast<float>(i) / steps;
            for (int j = 0; j < steps * 2; ++j)
            {
                const float azimuth = std::numbers::pi_v<float> * static_cast<float>(j) / steps;
                const glm::vec3 direction{ std::sin(polar) * std::cos(azimuth), std::sin(polar) * std::sin(azimuth), std::cos(polar) };
                const auto point = camera.project(sphere.center + direction * sphere.radius, aspect);
                REQUIRE(point);
                lowest = { std::min(lowest.x, point->x), std::min(lowest.y, point->y) };
                highest = { std::max(highest.x, point->x), std::max(highest.y, point->y) };
            }
        }
        CHECK(lowest.x >= rect->min.x - 1e-4f);
        CHECK(lowest.y >= rect->min.y - 1e-4f);
        CHECK(highest.x <= rect->max.x + 1e-4f);
        CHECK(highest.y <= rect->max.y + 1e-4f);
        CHECK(near(lowest, rect->min, 1e-3f));
        CHECK(near(highest, rect->max, 1e-3f));
    }

    TEST_CASE("projectSphere gives none for a sphere reaching behind the near plane")
    {
        const EditorCamera camera = originCamera();
        CHECK_FALSE(camera.projectSphere({ .center = { 0.0f, 0.5f, 0.0f }, .radius = 1.0f }, 1.0f));
        CHECK_FALSE(camera.projectSphere({ .center = { 0.0f, -5.0f, 0.0f }, .radius = 1.0f }, 1.0f));
        CHECK_FALSE(camera.projectSphere({ .center = { 3.0f, 1.0f, 0.0f }, .radius = 1.0f }, 1.0f));
    }
}

TEST_SUITE("editor::navigationAxes")
{
    using aegis::editor::NavigationAxis;

    TEST_CASE("each axis end points on screen as the camera sees it")
    {
        // Looking along +Y: +X is to the right, +Z up (y down on screen), +Y away from the viewer
        const auto axes = aegis::editor::navigationAxes(originCamera());
        const auto end = [&](int axis, bool positive) {
            return *std::ranges::find_if(axes, [&](const NavigationAxis& a) { return a.axis == axis && a.positive == positive; });
        };
        CHECK(near(end(0, true).offset, glm::vec2{ 1.0f, 0.0f }));
        CHECK(near(end(0, false).offset, glm::vec2{ -1.0f, 0.0f }));
        CHECK(near(end(2, true).offset, glm::vec2{ 0.0f, -1.0f }));
        CHECK(near(end(1, true).offset, glm::vec2{ 0.0f, 0.0f }));
        CHECK(end(1, true).depth == doctest::Approx(-1.0f));
        CHECK(end(1, false).depth == doctest::Approx(1.0f));
        CHECK(near(end(2, false).direction, glm::vec3{ 0.0f, 0.0f, -1.0f }));
    }

    TEST_CASE("the ends come farthest first")
    {
        EditorCamera camera;
        camera.lookAlong(glm::vec3{ 0.0f }, glm::vec3{ 1.0f, 2.0f, -3.0f });
        const auto axes = aegis::editor::navigationAxes(camera);
        CHECK(std::ranges::is_sorted(axes, std::less{}, &NavigationAxis::depth));
    }

    TEST_CASE("a click picks the disc under it, the nearest where discs overlap")
    {
        // Looking along +Y, +Y and -Y both sit at the center: -Y points at the viewer
        const auto axes = aegis::editor::navigationAxes(originCamera());
        const auto center = aegis::editor::pickNavigationAxis(axes, glm::vec2{ 0.05f, 0.0f }, 0.2f);
        REQUIRE(center);
        CHECK(center->axis == 1);
        CHECK_FALSE(center->positive);
        const auto right = aegis::editor::pickNavigationAxis(axes, glm::vec2{ 0.9f, 0.1f }, 0.2f);
        REQUIRE(right);
        CHECK(right->axis == 0);
        CHECK(right->positive);
        CHECK_FALSE(aegis::editor::pickNavigationAxis(axes, glm::vec2{ 0.5f, 0.5f }, 0.2f));
    }
}
