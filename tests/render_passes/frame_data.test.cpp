#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_passes;

namespace
{
using aegis::render_graph::FrameScene;
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

TEST_SUITE("render_graph::detail::makeCameraData")
{
    using aegis::render_graph::detail::makeCameraData;

    TEST_CASE("packs the matrices row-major and takes the camera position")
    {
        FrameScene::Camera camera{ .position = { 5.0f, 6.0f, 7.0f } };
        camera.view[3] = glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f }; // glm is column-major: the translation is column 3
        camera.projection[0][0] = 2.0f;
        camera.projection[1][1] = 3.0f;

        const auto data = makeCameraData(camera);
        CHECK(equal(data.position, glm::vec3{ 5.0f, 6.0f, 7.0f }));
        CHECK(equal(data.view, glm::rowMajor4(camera.view)));
        CHECK(equal(data.projection, glm::rowMajor4(camera.projection)));
        CHECK(equal(data.viewProjection, glm::rowMajor4(camera.projection * camera.view)));
        // Row-major: the translation sits in the last column of each row, i.e. in the w of the first three vectors.
        CHECK(data.view[0].w == 1.0f);
        CHECK(data.view[1].w == 2.0f);
        CHECK(data.view[2].w == 3.0f);
    }
}

TEST_SUITE("render_graph::detail::packLighting")
{
    using aegis::render_graph::LightingViewMode;
    using aegis::render_graph::detail::packLighting;

    TEST_CASE("an empty scene keeps the defaults and the given settings")
    {
        const auto uniforms = packLighting(FrameScene{ .instances = InstanceStore{} }, 0.5f, LightingViewMode::Albedo);
        CHECK(uniforms.ambientOcclusionFactor == 0.5f);
        CHECK(uniforms.viewMode == LightingViewMode::Albedo);
        CHECK(uniforms.pointLightCount == 0);
        CHECK(equal(uniforms.cameraPosition, glm::vec4{ 0.0f }));
        CHECK(equal(uniforms.ambientColor, glm::vec4{ 0.0f }));
        CHECK(equal(uniforms.directionalColor, glm::vec4{ 0.0f }));
        CHECK(equal(uniforms.directionalDirection, glm::vec4{ 0.0f, 0.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("packs camera, ambient and directional light")
    {
        const InstanceStore instances;
        const FrameScene scene{
            .instances = instances,
            .camera = FrameScene::Camera{ .position = { 1.0f, 2.0f, 3.0f } },
            .ambientLight = FrameScene::AmbientLight{ .color = { 0.1f, 0.2f, 0.3f }, .intensity = 0.5f },
            .directionalLight = FrameScene::DirectionalLight{
                .color = { 1.0f, 0.5f, 0.25f }, .intensity = 2.0f, .direction = { 0.0f, -1.0f, 0.0f } },
        };

        const auto uniforms = packLighting(scene, 1.0f, LightingViewMode::SceneColor);
        CHECK(equal(uniforms.cameraPosition, glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f }));
        CHECK(equal(uniforms.ambientColor, glm::vec4{ 0.1f, 0.2f, 0.3f, 0.5f }));
        CHECK(equal(uniforms.directionalColor, glm::vec4{ 1.0f, 0.5f, 0.25f, 2.0f }));
        CHECK(equal(uniforms.directionalDirection, glm::vec4{ 0.0f, -1.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("packs point lights and stops at the maximum")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances };
        for (std::uint32_t i = 0; i < aegis::render_graph::maxPointLights + 10; ++i)
        {
            scene.pointLights.push_back(
                { .position = { static_cast<float>(i), 0.0f, 0.0f }, .color = { 1.0f, 0.0f, 0.0f }, .intensity = 3.0f });
        }

        const auto uniforms = packLighting(scene, 1.0f, LightingViewMode::SceneColor);
        CHECK(uniforms.pointLightCount == static_cast<std::int32_t>(aegis::render_graph::maxPointLights));
        CHECK(equal(uniforms.pointLights[1].position, glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f }));
        CHECK(equal(uniforms.pointLights[0].color, glm::vec4{ 1.0f, 0.0f, 0.0f, 3.0f }));
    }
}

TEST_SUITE("render_graph::detail::makeSkyBoxViewProjection")
{
    using aegis::render_graph::detail::makeSkyBoxViewProjection;

    TEST_CASE("drops the view's translation and keeps its rotation")
    {
        FrameScene::Camera camera;
        // A quarter turn about +Y (columns: the rotated axes).
        camera.view[0] = glm::vec4{ 0.0f, 0.0f, -1.0f, 0.0f };
        camera.view[2] = glm::vec4{ 1.0f, 0.0f, 0.0f, 0.0f };
        const auto rotationOnly = camera.view;
        camera.view[3] = glm::vec4{ 10.0f, 20.0f, 30.0f, 1.0f };
        camera.projection[0][0] = 2.0f;

        CHECK(equal(makeSkyBoxViewProjection(camera), glm::rowMajor4(camera.projection * rotationOnly)));
    }
}

TEST_SUITE("render_graph::detail::makePointLightConstants")
{
    using aegis::render_graph::detail::makePointLightConstants;

    TEST_CASE("places the billboard at the light and sizes it by intensity and scale")
    {
        const FrameScene::PointLight light{
            .position = { 1.0f, 2.0f, 3.0f }, .color = { 0.5f, 0.25f, 1.0f }, .intensity = 100.0f, .scale = 2.0f
        };

        const auto constants = makePointLightConstants(light, 42);
        CHECK(equal(constants.worldPosition, glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f }));
        CHECK(equal(constants.color, glm::vec4{ 0.5f, 0.25f, 1.0f, 1.0f }));
        CHECK(constants.radius == doctest::Approx(0.4f));
        CHECK(constants.camera == 42);
    }
}
