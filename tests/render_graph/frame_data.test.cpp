#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.scene;
import aegis.render_graph;

namespace
{
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

TEST_SUITE("render_graph::detail::makeCameraData")
{
    using aegis::render_graph::detail::makeCameraData;

    TEST_CASE("packs the matrices row-major and takes the position from the transform")
    {
        aegis::Camera camera;
        camera.viewMatrix[3] = glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f }; // glm is column-major: the translation is column 3
        camera.projectionMatrix[0][0] = 2.0f;
        camera.projectionMatrix[1][1] = 3.0f;
        aegis::GlobalTransform transform;
        transform.location = { 5.0f, 6.0f, 7.0f };

        const auto data = makeCameraData(camera, transform);
        CHECK(equal(data.position, glm::vec3{ 5.0f, 6.0f, 7.0f }));
        CHECK(equal(data.view, glm::rowMajor4(camera.viewMatrix)));
        CHECK(equal(data.projection, glm::rowMajor4(camera.projectionMatrix)));
        CHECK(equal(data.viewProjection, glm::rowMajor4(camera.projectionMatrix * camera.viewMatrix)));
        // Row-major: the translation sits in the last column of each row, i.e. in the w of the first three vectors.
        CHECK(data.view[0].w == 1.0f);
        CHECK(data.view[1].w == 2.0f);
        CHECK(data.view[2].w == 3.0f);
    }

    TEST_CASE("does not modify the camera")
    {
        aegis::Camera camera;
        camera.aspect = 1.5f;
        const aegis::GlobalTransform transform;
        static_cast<void>(makeCameraData(camera, transform));
        CHECK(camera.aspect == 1.5f);
    }
}

TEST_SUITE("render_graph::detail::gatherLighting")
{
    using aegis::render_graph::LightingViewMode;
    using aegis::render_graph::detail::gatherLighting;

    TEST_CASE("an empty scene keeps the defaults and the given settings")
    {
        aegis::scene::Scene scene;
        const auto uniforms = gatherLighting(scene, 0.5f, LightingViewMode::Albedo);
        CHECK(uniforms.ambientOcclusionFactor == 0.5f);
        CHECK(uniforms.viewMode == LightingViewMode::Albedo);
        CHECK(uniforms.pointLightCount == 0);
        CHECK(equal(uniforms.cameraPosition, glm::vec4{ 0.0f }));
        CHECK(equal(uniforms.ambientColor, glm::vec4{ 0.0f }));
        CHECK(equal(uniforms.directionalColor, glm::vec4{ 0.0f }));
        CHECK(equal(uniforms.directionalDirection, glm::vec4{ 0.0f, 0.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("gathers camera, ambient and directional light")
    {
        aegis::scene::Scene scene;
        auto& registry = scene.registry();

        const auto camera = registry.create("Camera", glm::vec3{ 1.0f, 2.0f, 3.0f });
        registry.add<aegis::Camera>(camera);
        registry.get<aegis::GlobalTransform>(camera).location = { 1.0f, 2.0f, 3.0f };
        scene.setMainCamera(camera);

        const auto ambient = registry.create("Ambient");
        registry.add<aegis::AmbientLight>(ambient, glm::vec3{ 0.1f, 0.2f, 0.3f }, 0.5f);
        scene.setAmbientLight(ambient);

        const auto sun = registry.create("Sun");
        registry.add<aegis::DirectionalLight>(sun, glm::vec3{ 1.0f, 0.5f, 0.25f }, 2.0f);
        registry.get<aegis::GlobalTransform>(sun).rotation = glm::quat{ glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f }) };
        scene.setDirectionalLight(sun);

        const auto uniforms = gatherLighting(scene, 1.0f, LightingViewMode::SceneColor);
        CHECK(equal(uniforms.cameraPosition, glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f }));
        CHECK(equal(uniforms.ambientColor, glm::vec4{ 0.1f, 0.2f, 0.3f, 0.5f }));
        CHECK(equal(uniforms.directionalColor, glm::vec4{ 1.0f, 0.5f, 0.25f, 2.0f }));
        CHECK(uniforms.directionalDirection.w == 0.0f);
        const auto direction = glm::vec3{ uniforms.directionalDirection };
        CHECK(glm::length(direction) == doctest::Approx(1.0f));
        const auto expected = glm::normalize(aegis::GlobalTransform{ .rotation = glm::quat{ glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f }) } }.forward());
        CHECK(direction.x == doctest::Approx(expected.x));
        CHECK(direction.y == doctest::Approx(expected.y));
        CHECK(direction.z == doctest::Approx(expected.z));
    }

    TEST_CASE("collects point lights and stops at the maximum")
    {
        aegis::scene::Scene scene;
        auto& registry = scene.registry();
        const auto count = aegis::render_graph::maxPointLights + 10;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto light = registry.create("Light");
            registry.add<aegis::PointLight>(light, glm::vec3{ 1.0f, 0.0f, 0.0f }, 3.0f);
            registry.get<aegis::GlobalTransform>(light).location = { static_cast<float>(i), 0.0f, 0.0f };
        }

        const auto uniforms = gatherLighting(scene, 1.0f, LightingViewMode::SceneColor);
        CHECK(uniforms.pointLightCount == static_cast<std::int32_t>(aegis::render_graph::maxPointLights));
        CHECK(uniforms.pointLights[0].position.w == 1.0f);
        CHECK(equal(uniforms.pointLights[0].color, glm::vec4{ 1.0f, 0.0f, 0.0f, 3.0f }));
    }
}
