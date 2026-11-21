#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_passes;
import aegis.rhi;

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

// A camera as aegis::CameraSystem builds it: right-handed, depth 0 to 1, y flipped.
auto perspective(float fov, float aspect, float near, float far) -> glm::mat4
{
    const float tanHalfFov = std::tan(fov / 2.0f);
    glm::mat4 projection{ 0.0f };
    projection[0][0] = 1.0f / (aspect * tanHalfFov);
    projection[1][1] = -1.0f / tanHalfFov;
    projection[2][2] = far / (near - far);
    projection[2][3] = -1.0f;
    projection[3][2] = -(far * near) / (far - near);
    return projection;
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
        const auto uniforms = packLighting(FrameScene{ .instances = InstanceStore{} }, 0, 0, 16, 0.5f, LightingViewMode::Albedo);
        CHECK(uniforms.ambientOcclusionFactor == 0.5f);
        CHECK(uniforms.viewMode == LightingViewMode::Albedo);
        CHECK(uniforms.pointLightCount == 0);
        CHECK(uniforms.spotLightCount == 0);
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
            .views = { { .name = "Main", .camera = FrameScene::Camera{ .position = { 1.0f, 2.0f, 3.0f } } } },
            .ambientLight = FrameScene::AmbientLight{ .color = { 0.1f, 0.2f, 0.3f }, .intensity = 0.5f },
            .directionalLight = FrameScene::DirectionalLight{
                .color = { 1.0f, 0.5f, 0.25f }, .intensity = 2.0f, .direction = { 0.0f, -1.0f, 0.0f } },
        };

        const auto uniforms = packLighting(scene, 0, 0, 16, 1.0f, LightingViewMode::SceneColor);
        CHECK(equal(uniforms.cameraPosition, glm::vec4{ 1.0f, 2.0f, 3.0f, 1.0f }));
        CHECK(equal(uniforms.ambientColor, glm::vec4{ 0.1f, 0.2f, 0.3f, 0.5f }));
        CHECK(equal(uniforms.directionalColor, glm::vec4{ 1.0f, 0.5f, 0.25f, 2.0f }));
        CHECK(equal(uniforms.directionalDirection, glm::vec4{ 0.0f, -1.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("takes the camera of the view it lights")
    {
        const InstanceStore instances;
        const FrameScene scene{
            .instances = instances,
            .views = {
                { .name = "Main", .camera = FrameScene::Camera{ .position = { 1.0f, 2.0f, 3.0f } } },
                { .name = "Play", .camera = FrameScene::Camera{ .position = { 4.0f, 5.0f, 6.0f } } },
                { .name = "Empty" },
            },
        };

        CHECK(equal(packLighting(scene, 1, 0, 16, 1.0f, LightingViewMode::SceneColor).cameraPosition, glm::vec4{ 4.0f, 5.0f, 6.0f, 1.0f }));
        CHECK(equal(packLighting(scene, 2, 0, 16, 1.0f, LightingViewMode::SceneColor).cameraPosition, glm::vec4{ 0.0f }));
        CHECK(equal(packLighting(scene, 3, 0, 16, 1.0f, LightingViewMode::SceneColor).cameraPosition, glm::vec4{ 0.0f }));
    }

    TEST_CASE("shadows the directional light by its cascades' maps")
    {
        const InstanceStore instances;
        const FrameScene::Camera camera{
            .view = aegis::math::viewMatrix({ 0.0f, 0.0f, 2.0f }, { 0.0f, 1.0f, 0.0f }),
            .projection = perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f),
            .position = { 0.0f, 0.0f, 2.0f },
        };
        FrameScene scene{
            .instances = instances,
            .views = { { .name = "Main", .camera = camera } },
            .directionalLight = FrameScene::DirectionalLight{ .direction = { 0.0f, 0.0f, 1.0f }, .shadowBias = 1.5f, .shadowCascades = 3 },
        };
        aegis::render_graph::addShadowViews(scene);
        aegis::render_graph::detail::ShadowMapInput shadow{ .view = 1 };
        for (std::uint32_t cascade = 0; cascade < aegis::render_graph::maxShadowCascades; ++cascade)
            shadow.maps[cascade] = { { 7 + cascade }, { 3 } };

        const auto uniforms = packLighting(scene, 0, 0, 16, 1.0f, LightingViewMode::SceneColor, shadow);
        REQUIRE(uniforms.shadowCascadeCount == 3);
        CHECK(uniforms.shadowBias == 1.5f);
        CHECK(uniforms.shadowCascadeBlend == aegis::render_graph::shadowCascadeBlend);
        // The axis the cascades split: the camera's forward.
        CHECK(equal(uniforms.cameraForward, glm::vec4{ 0.0f, 1.0f, 0.0f, 0.0f }));
        const auto splits = aegis::render_graph::shadowCascadeSplits(*scene.directionalLight, camera.projection);
        for (std::uint32_t cascade = 0; cascade < 3; ++cascade)
        {
            CAPTURE(cascade);
            const auto& data = uniforms.shadowCascades[cascade];
            const auto& shadowCamera = *scene.views[1 + cascade].camera;
            CHECK(equal(data.viewProjection, glm::rowMajor4(shadowCamera.projection * shadowCamera.view)));
            CHECK(data.map.image.index == 7 + cascade);
            CHECK(data.map.sampler.index == 3);
            CHECK(data.splitFar == splits[cascade]);
            // The orthographic projection is 2 / projection[0][0] meters across the map's texels.
            CHECK(data.texelSize == doctest::Approx(2.0f / (shadowCamera.projection[0][0] * 2048.0f)));
        }
        // Each cascade is wider than the one before, so its texels are larger.
        CHECK(uniforms.shadowCascades[1].texelSize > uniforms.shadowCascades[0].texelSize);
        CHECK(uniforms.shadowCascades[2].texelSize > uniforms.shadowCascades[1].texelSize);
    }

    TEST_CASE("leaves the directional light unshadowed without shadow maps, a light or shadow cameras")
    {
        const InstanceStore instances;
        FrameScene scene{
            .instances = instances,
            .views = { { .name = "Main", .camera = FrameScene::Camera{} } },
            .directionalLight = FrameScene::DirectionalLight{},
        };
        aegis::render_graph::addShadowViews(scene);
        const aegis::render_graph::detail::ShadowMapInput shadow{ .view = 1 };
        CHECK(packLighting(scene, 0, 0, 16, 1.0f, LightingViewMode::SceneColor).shadowCascadeCount == 0);

        auto unlit = scene;
        unlit.directionalLight.reset();
        CHECK(packLighting(unlit, 0, 0, 16, 1.0f, LightingViewMode::SceneColor, shadow).shadowCascadeCount == 0);

        auto noCamera = scene;
        noCamera.views[1].camera.reset();
        CHECK(packLighting(noCamera, 0, 0, 16, 1.0f, LightingViewMode::SceneColor, shadow).shadowCascadeCount == 0);

        // The cascades end at the first without a camera.
        auto twoCascades = scene;
        twoCascades.views[3].camera.reset();
        CHECK(packLighting(twoCascades, 0, 0, 16, 1.0f, LightingViewMode::SceneColor, shadow).shadowCascadeCount == 2);
    }

    TEST_CASE("points at the light buffer and counts the lights that fit, point lights first")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances };
        scene.pointLights.resize(3);
        scene.spotLights.resize(4);

        const auto all = packLighting(scene, 0, 0x1000, 16, 1.0f, LightingViewMode::SceneColor);
        CHECK(all.lights == 0x1000);
        CHECK(all.pointLightCount == 3);
        CHECK(all.spotLightCount == 4);

        const auto someSpots = packLighting(scene, 0, 0x1000, 5, 1.0f, LightingViewMode::SceneColor);
        CHECK(someSpots.pointLightCount == 3);
        CHECK(someSpots.spotLightCount == 2);

        const auto somePoints = packLighting(scene, 0, 0x1000, 2, 1.0f, LightingViewMode::SceneColor);
        CHECK(somePoints.pointLightCount == 2);
        CHECK(somePoints.spotLightCount == 0);
    }
}

TEST_SUITE("render_graph::detail::packLights")
{
    using aegis::render_graph::detail::coneAttenuation;
    using aegis::render_graph::detail::packLight;
    using aegis::render_graph::detail::packLights;

    TEST_CASE("packs the point lights, then the spot lights, each in the scene's order")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances };
        scene.spotLights.push_back({ .position = { 10.0f, 0.0f, 0.0f } });
        scene.pointLights.push_back({ .position = { 1.0f, 0.0f, 0.0f } });
        scene.pointLights.push_back({ .position = { 2.0f, 0.0f, 0.0f } });
        scene.spotLights.push_back({ .position = { 20.0f, 0.0f, 0.0f } });

        const auto lights = packLights(scene);
        REQUIRE(lights.size() == 4);
        CHECK(lights[0].position.x == 1.0f);
        CHECK(lights[1].position.x == 2.0f);
        CHECK(lights[2].position.x == 10.0f);
        CHECK(lights[3].position.x == 20.0f);
    }

    TEST_CASE("packs every light of a scene with more than the old fixed maximum of 128")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances };
        scene.pointLights.resize(1024);
        CHECK(packLights(scene).size() == 1024);
    }

    TEST_CASE("a point light keeps its values and a cone that lets everything through")
    {
        const FrameScene::PointLight light{
            .position = { 1.0f, 2.0f, 3.0f }, .color = { 0.5f, 0.25f, 1.0f }, .intensity = 7.0f, .range = 12.0f
        };

        const auto data = packLight(light);
        CHECK(equal(data.position, glm::vec3{ 1.0f, 2.0f, 3.0f }));
        CHECK(equal(data.color, glm::vec3{ 0.5f, 0.25f, 1.0f }));
        CHECK(data.intensity == 7.0f);
        CHECK(data.range == 12.0f);
        CHECK(data.coneScale == 0.0f);
        CHECK(data.coneOffset == 1.0f);
        for (const auto toSurface : { glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, -1.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f } })
            CHECK(coneAttenuation(data, toSurface) == 1.0f);
    }

    TEST_CASE("a spot light lights its inner cone fully, fades to its outer cone and lights nothing beyond")
    {
        const FrameScene::SpotLight light{
            .position = { 0.0f, 5.0f, 0.0f },
            .direction = { 0.0f, -1.0f, 0.0f },
            .range = 10.0f,
            .innerConeAngle = glm::radians(20.0f),
            .outerConeAngle = glm::radians(40.0f),
        };
        const auto data = packLight(light);
        CHECK(equal(data.direction, glm::vec3{ 0.0f, -1.0f, 0.0f }));
        CHECK(data.range == 10.0f);

        // A direction at 'degrees' from the cone's axis.
        const auto at = [](float degrees) {
            const float angle = glm::radians(degrees);
            return glm::vec3{ std::sin(angle), -std::cos(angle), 0.0f };
        };
        CHECK(coneAttenuation(data, at(0.0f)) == doctest::Approx(1.0f));
        CHECK(coneAttenuation(data, at(19.0f)) == doctest::Approx(1.0f));
        const float between = coneAttenuation(data, at(30.0f));
        CHECK(between > 0.0f);
        CHECK(between < 1.0f);
        CHECK(coneAttenuation(data, at(41.0f)) == 0.0f);
        CHECK(coneAttenuation(data, at(90.0f)) == 0.0f);
        CHECK(coneAttenuation(data, at(180.0f)) == 0.0f);
    }

    TEST_CASE("a spot light whose outer cone is narrower than its inner one has a hard edge at the outer cone")
    {
        const FrameScene::SpotLight light{
            .direction = { 0.0f, 0.0f, 1.0f },
            .innerConeAngle = glm::radians(30.0f),
            .outerConeAngle = glm::radians(10.0f),
        };
        const auto data = packLight(light);
        CHECK(std::isfinite(data.coneScale));
        CHECK(coneAttenuation(data, { 0.0f, 0.0f, 1.0f }) == doctest::Approx(1.0f));
        const float angle = glm::radians(20.0f);
        CHECK(coneAttenuation(data, { std::sin(angle), 0.0f, std::cos(angle) }) == 0.0f);
    }
}

TEST_SUITE("render_graph::detail::distanceAttenuation")
{
    using aegis::render_graph::detail::distanceAttenuation;

    TEST_CASE("falls off with the inverse square close to the light")
    {
        // Far inside the range the window is about 1.
        CHECK(distanceAttenuation(1.0f, 100.0f) == doctest::Approx(1.0f).epsilon(1e-4));
        CHECK(distanceAttenuation(2.0f, 100.0f) == doctest::Approx(0.25f).epsilon(1e-4));
    }

    TEST_CASE("reaches zero at the range and stays there")
    {
        CHECK(distanceAttenuation(20.0f, 20.0f) == 0.0f);
        CHECK(distanceAttenuation(25.0f, 20.0f) == 0.0f);
        CHECK(distanceAttenuation(19.0f, 20.0f) > 0.0f);
    }

    TEST_CASE("decreases with distance up to the range")
    {
        float previous = distanceAttenuation(0.5f, 20.0f);
        for (float distance = 1.0f; distance <= 20.0f; distance += 0.5f)
        {
            const float current = distanceAttenuation(distance, 20.0f);
            CHECK(current < previous);
            previous = current;
        }
    }

    TEST_CASE("windows the inverse square as glTF's KHR_lights_punctual does")
    {
        // At half the range: (1 - 0.5^4)^2 / 10^2.
        CHECK(distanceAttenuation(10.0f, 20.0f) == doctest::Approx((1.0f - 0.0625f) * (1.0f - 0.0625f) / 100.0f));
    }

    TEST_CASE("stays finite at the light and with a zero range")
    {
        CHECK(std::isfinite(distanceAttenuation(0.0f, 20.0f)));
        CHECK(distanceAttenuation(1.0f, 0.0f) == 0.0f);
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
