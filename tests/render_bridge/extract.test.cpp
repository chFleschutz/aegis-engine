#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_bridge;
import aegis.render_graph;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;

namespace
{
using aegis::render_graph::InstanceStore;

// glm's operator== is not part of aegis.math's exports.
auto equal(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

auto equal(const glm::vec4& a, const glm::vec4& b) -> bool
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

auto equal(const glm::mat4& a, const glm::mat4& b) -> bool
{
    return equal(a[0], b[0]) && equal(a[1], b[1]) && equal(a[2], b[2]) && equal(a[3], b[3]);
}
}

TEST_SUITE("render_bridge::extractFrameScene")
{
    TEST_CASE("an empty world has no camera, lights or environment")
    {
        aegis::world::World world;
        const InstanceStore instances;
        const auto scene = aegis::extractFrameScene(world, instances);
        CHECK_FALSE(scene.camera.has_value());
        CHECK_FALSE(scene.ambientLight.has_value());
        CHECK_FALSE(scene.directionalLight.has_value());
        CHECK(scene.pointLights.empty());
        CHECK_FALSE(scene.environment.irradiance.has_value());
        CHECK_FALSE(scene.environment.prefiltered.has_value());
        CHECK_FALSE(scene.environment.skybox.has_value());
    }

    TEST_CASE("copies the main camera's matrices and position")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const auto camera = registry.create("Camera");
        auto& component = registry.add<aegis::Camera>(camera);
        component.viewMatrix[3] = glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f };
        component.projectionMatrix[0][0] = 2.0f;
        component.aspect = 1.5f;
        registry.get<aegis::GlobalTransform>(camera).location = { 5.0f, 6.0f, 7.0f };
        world.setMainCamera(camera);

        const auto scene = aegis::extractFrameScene(world, instances);
        REQUIRE(scene.camera.has_value());
        CHECK(equal(scene.camera->view, component.viewMatrix));
        CHECK(equal(scene.camera->projection, component.projectionMatrix));
        CHECK(equal(scene.camera->position, glm::vec3{ 5.0f, 6.0f, 7.0f }));
        CHECK(component.aspect == 1.5f); // Read, not written
    }

    TEST_CASE("copies ambient and directional light with the direction normalized")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const auto ambient = registry.create("Ambient");
        registry.add<aegis::AmbientLight>(ambient, glm::vec3{ 0.1f, 0.2f, 0.3f }, 0.5f);
        const auto sun = registry.create("Sun");
        registry.add<aegis::DirectionalLight>(sun, glm::vec3{ 1.0f, 0.5f, 0.25f }, 2.0f);
        const auto rotation = glm::quat{ glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f }) };
        registry.get<aegis::GlobalTransform>(sun).rotation = rotation;

        const auto scene = aegis::extractFrameScene(world, instances);
        REQUIRE(scene.ambientLight.has_value());
        CHECK(equal(scene.ambientLight->color, glm::vec3{ 0.1f, 0.2f, 0.3f }));
        CHECK(scene.ambientLight->intensity == 0.5f);
        REQUIRE(scene.directionalLight.has_value());
        CHECK(equal(scene.directionalLight->color, glm::vec3{ 1.0f, 0.5f, 0.25f }));
        CHECK(scene.directionalLight->intensity == 2.0f);
        const auto expected = glm::normalize(aegis::GlobalTransform{ .rotation = rotation }.forward());
        CHECK(glm::length(scene.directionalLight->direction) == doctest::Approx(1.0f));
        CHECK(scene.directionalLight->direction.x == doctest::Approx(expected.x));
        CHECK(scene.directionalLight->direction.y == doctest::Approx(expected.y));
        CHECK(scene.directionalLight->direction.z == doctest::Approx(expected.z));
    }

    TEST_CASE("copies every point light with its position and x scale")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const std::uint32_t count{ 138 }; // More than the lighting pass shades (render_graph::maxPointLights, 128)
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto light = registry.create("Light");
            registry.add<aegis::PointLight>(light, glm::vec3{ 1.0f, 0.0f, 0.0f }, 3.0f);
            auto& transform = registry.get<aegis::GlobalTransform>(light);
            transform.location = { static_cast<float>(i), 0.0f, 0.0f };
            transform.scale = { 2.0f, 4.0f, 8.0f };
        }

        const auto scene = aegis::extractFrameScene(world, instances);
        REQUIRE(scene.pointLights.size() == count);
        CHECK(equal(scene.pointLights[0].color, glm::vec3{ 1.0f, 0.0f, 0.0f }));
        CHECK(scene.pointLights[0].intensity == 3.0f);
        CHECK(scene.pointLights[0].scale == 2.0f);
    }

    TEST_CASE("a sky without an environment leaves the environment empty")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        registry.add<aegis::Sky>(registry.create("Sky"));

        const auto scene = aegis::extractFrameScene(world, instances);
        CHECK_FALSE(scene.environment.irradiance.has_value());
        CHECK_FALSE(scene.environment.prefiltered.has_value());
        CHECK_FALSE(scene.environment.skybox.has_value());
    }
}

TEST_SUITE("render_bridge::extractFrameScene")
{
    TEST_CASE("the frame scene names the store it was given")
    {
        aegis::world::World world;
        const InstanceStore instances;
        const auto scene = aegis::extractFrameScene(world, instances);
        CHECK(&scene.instances == &instances);
    }
}
