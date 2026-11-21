#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_bridge;
import aegis.render_passes;
import aegis.world;

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
    TEST_CASE("an empty world has the main view without a camera, and no lights or environment")
    {
        aegis::world::World world;
        const InstanceStore instances;
        const auto scene = aegis::extractFrameScene(world, instances);
        REQUIRE(scene.views.size() == 1);
        CHECK(scene.views[0].name == "Main");
        CHECK(scene.views[0].kind == aegis::render_graph::ViewKind::Main);
        CHECK_FALSE(scene.views[0].extent.has_value());
        CHECK_FALSE(scene.views[0].camera.has_value());
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
        registry.add<aegis::Camera>(camera);
        auto& matrices = registry.get<aegis::CameraMatrices>(camera);
        matrices.view[3] = glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f };
        matrices.projection[0][0] = 2.0f;
        matrices.aspect = 1.5f;
        registry.get<aegis::GlobalTransform>(camera).location = { 5.0f, 6.0f, 7.0f };
        world.setMainCamera(camera);

        const auto scene = aegis::extractFrameScene(world, instances);
        const auto mainCamera = scene.camera(0);
        REQUIRE(mainCamera.has_value());
        CHECK(equal(mainCamera->view, matrices.view));
        CHECK(equal(mainCamera->projection, matrices.projection));
        CHECK(equal(mainCamera->position, glm::vec3{ 5.0f, 6.0f, 7.0f }));
        CHECK(matrices.aspect == 1.5f); // Read, not written
    }

    TEST_CASE("without a PostProcess on the main camera the frame has the default settings")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const auto camera = registry.create("Camera");
        registry.add<aegis::Camera>(camera);
        registry.add<aegis::PostProcess>(registry.create("Other"), aegis::PostProcess{ .exposure = 3.0f });
        world.setMainCamera(camera);

        const auto settings = aegis::extractFrameScene(world, instances).postProcess;
        // The component's defaults are the frame's
        const aegis::PostProcess component;
        CHECK(settings.toneMapping == aegis::render_graph::ToneMapping::Aces);
        CHECK(component.toneMapping == aegis::ToneMapping::Aces);
        CHECK(settings.exposure == component.exposure);
        CHECK(settings.gamma == component.gamma);
        CHECK(settings.bloomIntensity == component.bloomIntensity);
        CHECK(settings.bloomThreshold == component.bloomThreshold);
        CHECK(settings.bloomSoftThreshold == component.bloomSoftThreshold);
    }

    TEST_CASE("copies the main camera's post-processing settings")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const auto camera = registry.create("Camera");
        registry.add<aegis::Camera>(camera);
        registry.add<aegis::PostProcess>(camera, aegis::PostProcess{
                                                     .toneMapping = aegis::ToneMapping::Reinhard,
                                                     .exposure = 2.0f,
                                                     .gamma = 1.8f,
                                                     .bloomIntensity = 0.5f,
                                                     .bloomThreshold = 1.0f,
                                                     .bloomSoftThreshold = 0.25f,
                                                 });
        world.setMainCamera(camera);

        const auto settings = aegis::extractFrameScene(world, instances).postProcess;
        CHECK(settings.toneMapping == aegis::render_graph::ToneMapping::Reinhard);
        CHECK(settings.exposure == 2.0f);
        CHECK(settings.gamma == 1.8f);
        CHECK(settings.bloomIntensity == 0.5f);
        CHECK(settings.bloomThreshold == 1.0f);
        CHECK(settings.bloomSoftThreshold == 0.25f);
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

    TEST_CASE("copies every point light with its position, range and x scale")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const std::uint32_t count{ 1024 };
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto light = registry.create("Light");
            registry.add<aegis::PointLight>(light, glm::vec3{ 1.0f, 0.0f, 0.0f }, 3.0f, 12.0f);
            auto& transform = registry.get<aegis::GlobalTransform>(light);
            transform.location = { static_cast<float>(i), 0.0f, 0.0f };
            transform.scale = { 2.0f, 4.0f, 8.0f };
        }

        const auto scene = aegis::extractFrameScene(world, instances);
        REQUIRE(scene.pointLights.size() == count);
        CHECK(equal(scene.pointLights[0].color, glm::vec3{ 1.0f, 0.0f, 0.0f }));
        CHECK(scene.pointLights[0].intensity == 3.0f);
        CHECK(scene.pointLights[0].range == 12.0f);
        CHECK(scene.pointLights[0].scale == 2.0f);
    }

    TEST_CASE("copies a spot light with its position, cone and the direction of its transform's forward")
    {
        aegis::world::World world;
        const InstanceStore instances;
        auto& registry = world.registry();
        const auto light = registry.create("Spot");
        registry.add<aegis::SpotLight>(light, glm::vec3{ 0.0f, 1.0f, 0.0f }, 50.0f, 8.0f, 0.25f, 0.5f, true, 2.5f);
        auto& transform = registry.get<aegis::GlobalTransform>(light);
        transform.location = { 1.0f, 2.0f, 3.0f };
        transform.rotation = glm::quat{ glm::vec3{ 0.3f, 0.0f, 1.2f } };

        const auto scene = aegis::extractFrameScene(world, instances);
        REQUIRE(scene.spotLights.size() == 1);
        const auto& spot = scene.spotLights[0];
        CHECK(equal(spot.position, glm::vec3{ 1.0f, 2.0f, 3.0f }));
        CHECK(equal(spot.color, glm::vec3{ 0.0f, 1.0f, 0.0f }));
        CHECK(spot.intensity == 50.0f);
        CHECK(spot.range == 8.0f);
        CHECK(spot.innerConeAngle == 0.25f);
        CHECK(spot.outerConeAngle == 0.5f);
        CHECK(spot.castsShadows);
        CHECK(spot.shadowBias == 2.5f);
        const auto expected = glm::normalize(transform.forward());
        CHECK(glm::length(spot.direction) == doctest::Approx(1.0f));
        CHECK(spot.direction.x == doctest::Approx(expected.x));
        CHECK(spot.direction.y == doctest::Approx(expected.y));
        CHECK(spot.direction.z == doctest::Approx(expected.z));
        CHECK(scene.pointLights.empty());
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
