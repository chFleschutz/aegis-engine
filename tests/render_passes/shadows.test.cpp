#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_passes;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;

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

auto camera(const glm::vec3& position, const glm::vec3& forward) -> FrameScene::Camera
{
    return {
        .view = aegis::math::viewMatrix(position, forward),
        .projection = perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 1000.0f),
        .position = position,
    };
}

// The world-space corners of the camera's frustum between its near plane and 'distance' meters along its axis.
auto sliceCorners(const FrameScene::Camera& camera, float distance) -> std::vector<glm::vec3>
{
    const auto toWorld = glm::inverse(camera.view);
    const float tanHalfFov = 1.0f / -camera.projection[1][1];
    const float aspect = -camera.projection[1][1] / camera.projection[0][0];
    std::vector<glm::vec3> corners;
    for (const float depth : { 0.1f, distance })
    {
        for (const float x : { -1.0f, 1.0f })
        {
            for (const float y : { -1.0f, 1.0f })
            {
                const glm::vec4 point{ x * depth * tanHalfFov * aspect, y * depth * tanHalfFov, -depth, 1.0f };
                corners.push_back(glm::vec3{ toWorld * point });
            }
        }
    }
    return corners;
}

auto project(const FrameScene::Camera& camera, const glm::vec3& point) -> glm::vec3
{
    const auto clip = camera.projection * camera.view * glm::vec4{ point, 1.0f };
    return glm::vec3{ clip } / clip.w;
}

auto near(float a, float b, float epsilon = 1e-4f) -> bool
{
    return std::abs(a - b) <= epsilon;
}

// glm's operator== is not part of aegis.math's exports.
auto equal(const glm::mat4& a, const glm::mat4& b) -> bool
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (a[column][row] != b[column][row])
                return false;
        }
    }
    return true;
}

auto finite(const glm::mat4& matrix) -> bool
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrix[column][row]))
                return false;
        }
    }
    return true;
}

const FrameScene::DirectionalLight sun{ .direction = glm::normalize(glm::vec3{ -0.35f, 0.35f, 0.87f }) };
}

TEST_SUITE("render_passes::fitDirectionalShadow")
{
    TEST_CASE("the shadow covers the camera's frustum up to the shadow distance")
    {
        const auto view = camera({ 3.0f, -2.0f, 1.5f }, { 0.3f, 1.0f, -0.2f });
        const auto shadow = fitDirectionalShadow(sun, view);
        for (const auto& corner : sliceCorners(view, sun.shadowDistance))
        {
            const auto ndc = project(shadow, corner);
            CAPTURE(ndc.x);
            CAPTURE(ndc.y);
            CAPTURE(ndc.z);
            CHECK(std::abs(ndc.x) <= 1.0f);
            CHECK(std::abs(ndc.y) <= 1.0f);
            CHECK(ndc.z >= 0.0f);
            CHECK(ndc.z <= 1.0f);
        }
    }

    TEST_CASE("the shadow reaches towards the light beyond the frustum, for casters above it")
    {
        const auto view = camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f });
        const auto shadow = fitDirectionalShadow(sun, view);
        // A point just inside the reach, above the camera along the light's direction.
        const auto caster = view.position + sun.direction * (shadowCasterReach * 0.9f);
        const auto ndc = project(shadow, caster);
        CHECK(ndc.z >= 0.0f);
        CHECK(ndc.z < project(shadow, view.position).z);
    }

    TEST_CASE("the shadow looks along the light with an orthographic projection")
    {
        const auto shadow = fitDirectionalShadow(sun, camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f }));
        CHECK(shadow.projection[3][3] == 1.0f); // Orthographic: what the task shader's cone culling checks
        // The view's -Z is the direction the light shines in.
        const glm::vec3 forward{ -shadow.view[0][2], -shadow.view[1][2], -shadow.view[2][2] };
        CHECK(near(glm::dot(forward, -sun.direction), 1.0f));
        // Its position is in front of every point it covers, towards the light.
        CHECK(project(shadow, shadow.position).z == doctest::Approx(0.0f).epsilon(1e-4));
    }

    TEST_CASE("turning the camera keeps the shadow's size")
    {
        const glm::vec3 position{ 1.0f, 2.0f, 1.5f };
        const auto ahead = fitDirectionalShadow(sun, camera(position, { 0.0f, 1.0f, 0.0f }));
        const auto turned = fitDirectionalShadow(sun, camera(position, { 0.7f, -0.4f, 0.1f }));
        CHECK(ahead.projection[0][0] == doctest::Approx(turned.projection[0][0]).epsilon(1e-5));
        CHECK(ahead.projection[2][2] == doctest::Approx(turned.projection[2][2]).epsilon(1e-5));
    }

    TEST_CASE("the shadow moves in whole texels of its map")
    {
        const auto shadow = fitDirectionalShadow(sun, camera({ 0.37f, 1.91f, 1.5f }, { 0.0f, 1.0f, 0.0f }));
        const float texel = 2.0f / (shadow.projection[0][0] * static_cast<float>(shadowMapExtent.x));
        for (const float offset : { shadow.view[3][0], shadow.view[3][1] })
        {
            const float texels = offset / texel;
            CAPTURE(texels);
            CHECK(near(texels, std::round(texels), 1e-2f));
        }
    }

    TEST_CASE("a light shining straight down gives a valid shadow")
    {
        const FrameScene::DirectionalLight overhead{ .direction = { 0.0f, 0.0f, 1.0f } };
        const auto shadow = fitDirectionalShadow(overhead, camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f }));
        CHECK(finite(shadow.view));
        CHECK(finite(shadow.projection));
    }

    TEST_CASE("a longer shadow distance covers more")
    {
        const auto view = camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f });
        auto far = sun;
        far.shadowDistance = 200.0f;
        // A smaller scale is a wider projection.
        CHECK(fitDirectionalShadow(far, view).projection[0][0] < fitDirectionalShadow(sun, view).projection[0][0]);
    }
}

TEST_SUITE("render_passes::addShadowViews")
{
    TEST_CASE("each main view gets a shadow view of the directional light, fitted to its camera")
    {
        const InstanceStore instances;
        FrameScene scene{
            .instances = instances,
            .views = {
                { .name = "Edit", .camera = camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f }) },
                { .name = "Play", .camera = camera({ 5.0f, 0.0f, 1.5f }, { 1.0f, 0.0f, 0.0f }) },
            },
            .directionalLight = sun,
        };
        addShadowViews(scene);

        REQUIRE(scene.views.size() == 4);
        CHECK(scene.views[0].shadow == 2u);
        CHECK(scene.views[1].shadow == 3u);
        CHECK(scene.views[2].name == "EditShadow");
        CHECK(scene.views[3].name == "PlayShadow");
        for (const auto& view : std::span{ scene.views }.subspan(2))
        {
            CHECK(view.kind == ViewKind::Shadow);
            CHECK(view.extent == shadowMapExtent);
            CHECK_FALSE(view.shadow.has_value());
        }
        REQUIRE(scene.views[3].camera.has_value());
        CHECK(equal(scene.views[3].camera->view, fitDirectionalShadow(sun, *scene.views[1].camera).view));
    }

    TEST_CASE("without a directional light or a camera the shadow view has no camera")
    {
        const InstanceStore instances;
        FrameScene unlit{ .instances = instances, .views = { { .name = "Main", .camera = camera({}, { 0.0f, 1.0f, 0.0f }) } } };
        addShadowViews(unlit);
        REQUIRE(unlit.views.size() == 2);
        CHECK_FALSE(unlit.views[1].camera.has_value());

        FrameScene noCamera{ .instances = instances, .views = { { .name = "Main" } }, .directionalLight = sun };
        addShadowViews(noCamera);
        REQUIRE(noCamera.views.size() == 2);
        CHECK_FALSE(noCamera.views[1].camera.has_value());
    }

    TEST_CASE("a main view that has a shadow view gets no second one")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances, .views = { { .name = "Main" } } };
        addShadowViews(scene);
        addShadowViews(scene);
        CHECK(scene.views.size() == 2);

        std::vector<DefaultPassesDesc::View> views{ { .name = "Main" } };
        addShadowViews(views);
        addShadowViews(views);
        CHECK(views.size() == 2);
    }

    TEST_CASE("the graph's views and the frame scene's come in the same order")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances, .views = { { .name = "Edit" }, { .name = "Play" } } };
        std::vector<DefaultPassesDesc::View> views{ { .name = "Edit" }, { .name = "Play", .extent = aegis::rhi::Extent2D{ 320, 240 } } };
        addShadowViews(scene);
        addShadowViews(views);

        REQUIRE(views.size() == scene.views.size());
        for (std::size_t i = 0; i < views.size(); ++i)
        {
            CAPTURE(i);
            CHECK(views[i].name == scene.views[i].name);
            CHECK(views[i].kind == scene.views[i].kind);
            CHECK(views[i].shadow == scene.views[i].shadow);
        }
        CHECK(views[3].extent == shadowMapExtent);
    }
}
