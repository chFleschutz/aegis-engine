#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_passes;
import aegis.rhi;

namespace math = aegis::math;

namespace
{
using namespace aegis::render_graph;

auto camera(const math::Float3& position, const math::Float3& forward) -> FrameScene::Camera
{
    return {
        .view = aegis::math::viewMatrix(position, forward),
        .projection = aegis::math::perspective(math::radians(60.0f), 4.0f / 3.0f, 0.1f, 1000.0f), // As aegis::CameraSystem builds it
        .position = position,
    };
}

// The world-space corners of the camera's frustum between its near plane and 'distance' meters along its axis.
auto sliceCorners(const FrameScene::Camera& camera, float distance) -> std::vector<math::Float3>
{
    const auto toWorld = math::inverse(camera.view);
    const float tanHalfFov = 1.0f / -camera.projection[1, 1];
    const float aspect = -camera.projection[1, 1] / camera.projection[0, 0];
    std::vector<math::Float3> corners;
    for (const float depth : { 0.1f, distance })
    {
        for (const float x : { -1.0f, 1.0f })
        {
            for (const float y : { -1.0f, 1.0f })
            {
                const math::Float4 point{ x * depth * tanHalfFov * aspect, y * depth * tanHalfFov, -depth, 1.0f };
                corners.push_back(math::Float3{ toWorld * point });
            }
        }
    }
    return corners;
}

auto project(const FrameScene::Camera& camera, const math::Float3& point) -> math::Float3
{
    const auto clip = camera.projection * camera.view * math::Float4{ point, 1.0f };
    return math::Float3{ clip } / clip.w;
}

auto near(float a, float b, float epsilon = 1e-4f) -> bool
{
    return std::abs(a - b) <= epsilon;
}

// glm's operator== is not part of aegis.math's exports.
auto equal(const math::Float4x4& a, const math::Float4x4& b) -> bool
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if (a[row, column] != b[row, column])
                return false;
        }
    }
    return true;
}

auto finite(const math::Float4x4& matrix) -> bool
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            if (!std::isfinite(matrix[row, column]))
                return false;
        }
    }
    return true;
}

const FrameScene::DirectionalLight sun{ .direction = math::normalize(math::Float3{ -0.35f, 0.35f, 0.87f }) };

// The shadow of the camera's whole slice, from its near plane to the light's shadow distance: a single cascade's.
auto whole(const FrameScene::DirectionalLight& light, const FrameScene::Camera& camera) -> FrameScene::Camera
{
    return fitDirectionalShadow(light, camera, 0.0f, light.shadowDistance);
}

// The world-space corners of the camera's frustum between 'near' and 'far' meters along its axis.
auto sliceCorners(const FrameScene::Camera& camera, float near, float far) -> std::vector<math::Float3>
{
    auto corners = sliceCorners(camera, far);
    const auto nearCorners = sliceCorners(camera, near);
    for (std::size_t i = 0; i < 4; ++i)
        corners[i] = nearCorners[i + 4];
    return corners;
}
}

TEST_SUITE("render_passes::fitDirectionalShadow")
{
    TEST_CASE("the shadow covers the camera's frustum up to the shadow distance")
    {
        const auto view = camera({ 3.0f, -2.0f, 1.5f }, { 0.3f, 1.0f, -0.2f });
        const auto shadow = whole(sun, view);
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
        const auto shadow = whole(sun, view);
        // A point just inside the reach, above the camera along the light's direction.
        const auto caster = view.position + sun.direction * (shadowCasterReach * 0.9f);
        const auto ndc = project(shadow, caster);
        CHECK(ndc.z >= 0.0f);
        CHECK(ndc.z < project(shadow, view.position).z);
    }

    TEST_CASE("the shadow looks along the light with an orthographic projection")
    {
        const auto shadow = whole(sun, camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f }));
        CHECK(shadow.projection[3, 3] == 1.0f); // Orthographic: what the task shader's cone culling checks
        // The view's -Z is the direction the light shines in.
        const auto forward = -math::Float3{ shadow.view.row(2) };
        CHECK(near(math::dot(forward, -sun.direction), 1.0f));
        // Its position is in front of every point it covers, towards the light.
        CHECK(project(shadow, shadow.position).z == doctest::Approx(0.0f).epsilon(1e-4));
    }

    TEST_CASE("turning the camera keeps the shadow's size")
    {
        const math::Float3 position{ 1.0f, 2.0f, 1.5f };
        const auto ahead = whole(sun, camera(position, { 0.0f, 1.0f, 0.0f }));
        const auto turned = whole(sun, camera(position, { 0.7f, -0.4f, 0.1f }));
        CHECK(ahead.projection[0, 0] == doctest::Approx(turned.projection[0, 0]).epsilon(1e-5));
        CHECK(ahead.projection[2, 2] == doctest::Approx(turned.projection[2, 2]).epsilon(1e-5));
    }

    TEST_CASE("the shadow moves in whole texels of its map")
    {
        const auto shadow = whole(sun, camera({ 0.37f, 1.91f, 1.5f }, { 0.0f, 1.0f, 0.0f }));
        const float texel = 2.0f / (shadow.projection[0, 0] * static_cast<float>(shadowMapExtent.x));
        for (const float offset : { shadow.view[0, 3], shadow.view[1, 3] })
        {
            const float texels = offset / texel;
            CAPTURE(texels);
            CHECK(near(texels, std::round(texels), 1e-2f));
        }
    }

    TEST_CASE("the snapped shadow still covers the slice's bounding sphere")
    {
        // Wherever the snap moves the map, the sphere around the slice stays inside it along both of its axes.
        for (int step = 0; step < 64; ++step)
        {
            const float offset = static_cast<float>(step) * 0.0137f;
            const auto view = camera({ offset, 2.0f * offset, 1.5f }, { 0.0f, 1.0f, 0.0f });
            const auto corners = sliceCorners(view, sun.shadowDistance);
            math::Float3 center{ 0.0f };
            for (const auto& corner : corners)
                center = center + corner;
            center = center / static_cast<float>(corners.size());
            float radius{ 0.0f };
            for (const auto& corner : corners)
                radius = std::max(radius, math::length(corner - center));

            const auto shadow = whole(sun, view);
            const math::Float3 side{ shadow.view.row(0) };
            const math::Float3 up{ shadow.view.row(1) };
            for (const auto& edge : { side, -side, up, -up })
            {
                const auto ndc = project(shadow, center + edge * radius);
                CAPTURE(step);
                CHECK(std::abs(ndc.x) <= 1.0f);
                CHECK(std::abs(ndc.y) <= 1.0f);
            }
        }
    }

    TEST_CASE("a light shining straight down gives a valid shadow")
    {
        const FrameScene::DirectionalLight overhead{ .direction = { 0.0f, 0.0f, 1.0f } };
        const auto shadow = whole(overhead, camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f }));
        CHECK(finite(shadow.view));
        CHECK(finite(shadow.projection));
    }

    TEST_CASE("a longer shadow distance covers more")
    {
        const auto view = camera({ 0.0f, 0.0f, 1.5f }, { 0.0f, 1.0f, 0.0f });
        auto far = sun;
        far.shadowDistance = 200.0f;
        // A smaller scale is a wider projection.
        CHECK(whole(far, view).projection[0, 0] < whole(sun, view).projection[0, 0]);
    }

    TEST_CASE("a cascade's shadow covers its slice of the frustum")
    {
        const auto view = camera({ 3.0f, -2.0f, 1.5f }, { 0.3f, 1.0f, -0.2f });
        const auto shadow = fitDirectionalShadow(sun, view, 8.0f, 20.0f);
        for (const auto& corner : sliceCorners(view, 8.0f, 20.0f))
        {
            const auto ndc = project(shadow, corner);
            CAPTURE(ndc.x);
            CAPTURE(ndc.y);
            CHECK(std::abs(ndc.x) <= 1.0f);
            CHECK(std::abs(ndc.y) <= 1.0f);
            CHECK(ndc.z >= 0.0f);
            CHECK(ndc.z <= 1.0f);
        }
        // A slice is narrower than the whole distance, so its map is sharper.
        CHECK(shadow.projection[0, 0] > whole(sun, view).projection[0, 0]);
    }

    TEST_CASE("a cascade's shadow moves in whole texels of its own map")
    {
        const auto shadow = fitDirectionalShadow(sun, camera({ 0.37f, 1.91f, 1.5f }, { 0.0f, 1.0f, 0.0f }), 8.0f, 20.0f);
        const float texel = 2.0f / (shadow.projection[0, 0] * static_cast<float>(shadowMapExtent.x));
        for (const float offset : { shadow.view[0, 3], shadow.view[1, 3] })
        {
            const float texels = offset / texel;
            CAPTURE(texels);
            CHECK(near(texels, std::round(texels), 1e-2f));
        }
    }
}

TEST_SUITE("render_passes::shadowCascadeSplits")
{
    const auto projection = aegis::math::perspective(math::radians(60.0f), 4.0f / 3.0f, 0.1f, 1000.0f);

    TEST_CASE("the cascades grow from the near plane to the shadow distance")
    {
        const auto splits = shadowCascadeSplits(sun, projection);
        REQUIRE(splits.size() == 4);
        CHECK(splits.front() > 0.1f);
        for (std::size_t i = 1; i < splits.size(); ++i)
        {
            CAPTURE(i);
            CHECK(splits[i] > splits[i - 1]);
            // Each cascade is at least as long as the one before.
            const float before = splits[i - 1] - (i == 1 ? 0.1f : splits[i - 2]);
            CHECK(splits[i] - splits[i - 1] >= before);
        }
        CHECK(splits.back() == sun.shadowDistance);
    }

    TEST_CASE("the split lambda mixes uniform and logarithmic splits")
    {
        auto light = sun;
        light.shadowSplitLambda = 0.0f;
        const auto uniform = shadowCascadeSplits(light, projection);
        REQUIRE(uniform.size() == 4);
        const float length = (light.shadowDistance - 0.1f) / 4.0f;
        for (std::size_t i = 0; i < uniform.size(); ++i)
        {
            CAPTURE(i);
            CHECK(uniform[i] == doctest::Approx(0.1f + length * static_cast<float>(i + 1)).epsilon(1e-4));
        }

        light.shadowSplitLambda = 1.0f;
        const auto logarithmic = shadowCascadeSplits(light, projection);
        REQUIRE(logarithmic.size() == 4);
        const float ratio = logarithmic[0] / 0.1f;
        for (std::size_t i = 1; i < logarithmic.size(); ++i)
        {
            CAPTURE(i);
            CHECK(logarithmic[i] / logarithmic[i - 1] == doctest::Approx(ratio).epsilon(1e-3));
        }

        // A mix lies between the two.
        const auto mixed = shadowCascadeSplits(sun, projection);
        for (std::size_t i = 0; i + 1 < mixed.size(); ++i)
        {
            CAPTURE(i);
            CHECK(mixed[i] > logarithmic[i]);
            CHECK(mixed[i] < uniform[i]);
        }
    }

    TEST_CASE("the cascade count is clamped to 1 to maxShadowCascades")
    {
        auto light = sun;
        light.shadowCascades = 0;
        CHECK(shadowCascadeSplits(light, projection).size() == 1);
        light.shadowCascades = 2;
        CHECK(shadowCascadeSplits(light, projection).size() == 2);
        light.shadowCascades = 9;
        CHECK(shadowCascadeSplits(light, projection).size() == maxShadowCascades);
    }

    TEST_CASE("the shadow ends at the camera's far plane")
    {
        auto light = sun;
        light.shadowDistance = 5000.0f;
        CHECK(shadowCascadeSplits(light, projection).back() == doctest::Approx(1000.0f).epsilon(1e-3));
    }
}

TEST_SUITE("render_passes::addShadowViews")
{
    TEST_CASE("each main view gets a shadow view per cascade of the directional light, fitted to its slice")
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

        REQUIRE(scene.views.size() == 2 + 2 * maxShadowCascades + maxShadowedSpotLights);
        CHECK(scene.views[0].shadow == 2u);
        CHECK(scene.views[1].shadow == 6u);
        CHECK(scene.views[2].name == "EditShadow0");
        CHECK(scene.views[5].name == "EditShadow3");
        CHECK(scene.views[6].name == "PlayShadow0");
        for (const auto& view : std::span{ scene.views }.subspan(2, 2 * maxShadowCascades))
        {
            CHECK_FALSE(view.tile.has_value());
            CHECK(view.kind == ViewKind::Shadow);
            CHECK(view.extent == shadowMapExtent);
            CHECK_FALSE(view.shadow.has_value());
            CHECK(view.camera.has_value());
        }

        const auto& play = *scene.views[1].camera;
        const auto splits = shadowCascadeSplits(sun, play.projection);
        REQUIRE(splits.size() == maxShadowCascades);
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
        {
            CAPTURE(cascade);
            const float near = cascade == 0 ? 0.0f : splits[cascade - 1];
            REQUIRE(scene.views[6 + cascade].camera.has_value());
            CHECK(equal(scene.views[6 + cascade].camera->view, fitDirectionalShadow(sun, play, near, splits[cascade]).view));
        }
    }

    TEST_CASE("each cascade's map covers the band where the one before blends into it")
    {
        // The lighting blends the far shadowCascadeBlend of each cascade's slice into the next cascade, so the next map
        // must reach back over that band: a point there outside it would read as lit.
        const InstanceStore instances;
        const auto view = camera({ 3.0f, -2.0f, 1.5f }, { 0.3f, 1.0f, -0.2f });
        FrameScene scene{ .instances = instances, .views = { { .name = "Main", .camera = view } }, .directionalLight = sun };
        addShadowViews(scene);
        const auto splits = shadowCascadeSplits(sun, view.projection);
        REQUIRE(splits.size() == maxShadowCascades);
        for (std::uint32_t cascade = 1; cascade < maxShadowCascades; ++cascade)
        {
            const float near = cascade == 1 ? 0.1f : splits[cascade - 2];
            const float far = splits[cascade - 1];
            const float band = (far - near) * shadowCascadeBlend;
            REQUIRE(scene.views[1 + cascade].camera.has_value());
            for (const auto& corner : sliceCorners(view, far - band, far))
            {
                const auto ndc = project(*scene.views[1 + cascade].camera, corner);
                CAPTURE(cascade);
                CAPTURE(ndc.x);
                CAPTURE(ndc.y);
                CHECK(std::abs(ndc.x) <= 1.0f);
                CHECK(std::abs(ndc.y) <= 1.0f);
                CHECK(ndc.z >= 0.0f);
                CHECK(ndc.z <= 1.0f);
            }
        }
    }

    TEST_CASE("cascades past the light's count have no camera")
    {
        const InstanceStore instances;
        auto light = sun;
        light.shadowCascades = 2;
        FrameScene scene{ .instances = instances, .views = { { .name = "Main", .camera = camera({}, { 0.0f, 1.0f, 0.0f }) } }, .directionalLight = light };
        addShadowViews(scene);
        REQUIRE(scene.views.size() == 1 + maxShadowCascades + maxShadowedSpotLights);
        CHECK(scene.views[1].camera.has_value());
        CHECK(scene.views[2].camera.has_value());
        CHECK_FALSE(scene.views[3].camera.has_value());
        CHECK_FALSE(scene.views[4].camera.has_value());
    }

    TEST_CASE("without a directional light or a camera the shadow views have no camera")
    {
        const InstanceStore instances;
        FrameScene unlit{ .instances = instances, .views = { { .name = "Main", .camera = camera({}, { 0.0f, 1.0f, 0.0f }) } } };
        addShadowViews(unlit);
        REQUIRE(unlit.views.size() == 1 + maxShadowCascades + maxShadowedSpotLights);
        for (const auto& view : std::span{ unlit.views }.subspan(1))
            CHECK_FALSE(view.camera.has_value());

        FrameScene noCamera{ .instances = instances, .views = { { .name = "Main" } }, .directionalLight = sun };
        addShadowViews(noCamera);
        REQUIRE(noCamera.views.size() == 1 + maxShadowCascades + maxShadowedSpotLights);
        for (const auto& view : std::span{ noCamera.views }.subspan(1))
            CHECK_FALSE(view.camera.has_value());
    }

    TEST_CASE("a main view that has shadow views gets no second set")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances, .views = { { .name = "Main" } } };
        addShadowViews(scene);
        addShadowViews(scene);
        CHECK(scene.views.size() == 1 + maxShadowCascades + maxShadowedSpotLights);

        std::vector<DefaultPassesDesc::View> views{ { .name = "Main" } };
        addShadowViews(views);
        addShadowViews(views);
        CHECK(views.size() == 1 + maxShadowCascades + maxShadowedSpotLights);
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
            CHECK(views[i].tile == scene.views[i].tile);
        }
        CHECK(views[2 + 2 * maxShadowCascades - 1].extent == shadowMapExtent);
        CHECK(views.back().extent == spotShadowTileExtent);
    }

    TEST_CASE("the closest shadow-casting spot lights get the spot shadow views, which all main views share")
    {
        const InstanceStore instances;
        FrameScene scene{
            .instances = instances,
            .views = {
                { .name = "Edit", .camera = camera({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) },
                { .name = "Play", .camera = camera({ 100.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }) },
            },
        };
        // Six casting lights 1 to 6 meters from the first main view's camera, out of order, and a closer one that doesn't
        // cast.
        for (const float distance : { 5.0f, 1.0f, 6.0f, 3.0f, 2.0f, 4.0f })
            scene.spotLights.push_back({ .position = { distance, 0.0f, 0.0f }, .direction = { 0.0f, 0.0f, -1.0f }, .castsShadows = true });
        scene.spotLights.push_back({ .position = { 0.5f, 0.0f, 0.0f }, .castsShadows = false });
        addShadowViews(scene);

        const auto first = static_cast<std::uint32_t>(2 + 2 * maxShadowCascades);
        REQUIRE(scene.views.size() == first + maxShadowedSpotLights);
        for (std::uint32_t index = 0; index < maxShadowedSpotLights; ++index)
        {
            CAPTURE(index);
            const auto& view = scene.views[first + index];
            CHECK(view.name == spotShadowViewName(index));
            CHECK(view.kind == ViewKind::Shadow);
            CHECK(view.extent == spotShadowTileExtent);
            CHECK(view.tile == spotShadowTile(index));
            CHECK(view.camera.has_value());
        }
        // The views go to the lights 1, 2, 3 and 4 meters away, closest first.
        CHECK(scene.spotLights[1].shadowView == first + 0);
        CHECK(scene.spotLights[4].shadowView == first + 1);
        CHECK(scene.spotLights[3].shadowView == first + 2);
        CHECK(scene.spotLights[5].shadowView == first + 3);
        CHECK_FALSE(scene.spotLights[0].shadowView.has_value());
        CHECK_FALSE(scene.spotLights[2].shadowView.has_value());
        CHECK_FALSE(scene.spotLights[6].shadowView.has_value());
        REQUIRE(scene.views[first].camera.has_value());
        CHECK(equal(scene.views[first].camera->view, fitSpotShadow(scene.spotLights[1]).view));
        CHECK(equal(scene.views[first].camera->projection, fitSpotShadow(scene.spotLights[1]).projection));
    }

    TEST_CASE("spot shadow views without a light to cast have no camera")
    {
        const InstanceStore instances;
        FrameScene scene{
            .instances = instances,
            .views = { { .name = "Main", .camera = camera({}, { 0.0f, 1.0f, 0.0f }) } },
            .spotLights = { { .position = { 1.0f, 0.0f, 0.0f }, .castsShadows = true } },
        };
        addShadowViews(scene);
        const auto first = 1 + maxShadowCascades;
        CHECK(scene.views[first].camera.has_value());
        for (std::uint32_t index = 1; index < maxShadowedSpotLights; ++index)
            CHECK_FALSE(scene.views[first + index].camera.has_value());

        // Without a camera to measure from, no light casts.
        FrameScene noCamera{ .instances = instances, .views = { { .name = "Main" } }, .spotLights = scene.spotLights };
        noCamera.spotLights[0].shadowView.reset();
        addShadowViews(noCamera);
        CHECK_FALSE(noCamera.spotLights[0].shadowView.has_value());
        for (std::uint32_t index = 0; index < maxShadowedSpotLights; ++index)
            CHECK_FALSE(noCamera.views[first + index].camera.has_value());
    }
}

TEST_SUITE("render_passes::selectShadowedSpotLights")
{
    TEST_CASE("picks the casting lights closest to the point, ties in the scene's order, up to the budget")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances };
        scene.spotLights = {
            { .position = { 3.0f, 0.0f, 0.0f }, .castsShadows = true },
            { .position = { 0.0f, 2.0f, 0.0f }, .castsShadows = true },
            { .position = { 0.0f, 0.0f, 1.0f }, .castsShadows = false },
            { .position = { -2.0f, 0.0f, 0.0f }, .castsShadows = true },
            { .position = { 0.0f, 0.0f, 4.0f }, .castsShadows = true },
        };
        CHECK(selectShadowedSpotLights(scene, math::Float3{ 0.0f }) == std::vector<std::uint32_t>{ 1, 3, 0, 4 });
        CHECK(selectShadowedSpotLights(scene, math::Float3{ 0.0f }, 2) == std::vector<std::uint32_t>{ 1, 3 });
        CHECK(selectShadowedSpotLights(scene, math::Float3{ 0.0f, 0.0f, 4.0f }, 1) == std::vector<std::uint32_t>{ 4 });
        CHECK(selectShadowedSpotLights(scene, math::Float3{ 0.0f }, 0).empty());
    }

    TEST_CASE("selects nothing without casting lights")
    {
        const InstanceStore instances;
        FrameScene scene{ .instances = instances, .spotLights = { { .castsShadows = false } } };
        CHECK(selectShadowedSpotLights(scene, math::Float3{ 0.0f }).empty());
    }
}

TEST_SUITE("render_passes::spotShadowTile")
{
    TEST_CASE("the tiles fill the atlas row by row without overlapping")
    {
        CHECK(spotShadowTile(0) == aegis::rhi::Rect2D{ .x = 0, .y = 0, .extent = spotShadowTileExtent });
        CHECK(spotShadowTile(1) == aegis::rhi::Rect2D{ .x = 1024, .y = 0, .extent = spotShadowTileExtent });
        CHECK(spotShadowTile(2) == aegis::rhi::Rect2D{ .x = 0, .y = 1024, .extent = spotShadowTileExtent });
        CHECK(spotShadowTile(3) == aegis::rhi::Rect2D{ .x = 1024, .y = 1024, .extent = spotShadowTileExtent });
        for (std::uint32_t index = 0; index < maxShadowedSpotLights; ++index)
        {
            const auto tile = spotShadowTile(index);
            CHECK(tile.x + tile.extent.x <= spotShadowAtlasExtent.x);
            CHECK(tile.y + tile.extent.y <= spotShadowAtlasExtent.y);
        }
        CHECK(spotShadowViewName(2) == "SpotShadow2");
    }
}

TEST_SUITE("render_passes::fitSpotShadow")
{
    TEST_CASE("covers the cone from near the light to its range")
    {
        const FrameScene::SpotLight light{
            .position = { 1.0f, 2.0f, 3.0f },
            .direction = math::normalize(math::Float3{ 1.0f, 1.0f, -0.5f }),
            .range = 10.0f,
            .outerConeAngle = math::radians(30.0f),
        };
        const auto shadow = fitSpotShadow(light);
        CHECK(shadow.position.x == light.position.x);

        // On the axis: the tile's center, depth between 0 and 1, growing with the distance.
        const auto middle = project(shadow, light.position + light.direction * 5.0f);
        CHECK(near(middle.x, 0.0f));
        CHECK(near(middle.y, 0.0f));
        CHECK(middle.z > 0.0f);
        CHECK(middle.z < 1.0f);
        CHECK(project(shadow, light.position + light.direction * 9.0f).z > middle.z);
        CHECK(near(project(shadow, light.position + light.direction * 10.0f).z, 1.0f));
        CHECK(project(shadow, light.position + light.direction * 0.01f).z < 0.0f); // Before the near plane

        // On the outer cone: the edge of the tile, whichever way round the axis.
        const auto side = math::normalize(math::cross(light.direction, math::Float3{ 0.0f, 0.0f, 1.0f }));
        const auto up = math::cross(side, light.direction);
        for (const auto& across : { side, up })
        {
            const auto edge = project(shadow, light.position + (light.direction * std::cos(light.outerConeAngle) + across * std::sin(light.outerConeAngle)) * 5.0f);
            CHECK(near(std::max(std::abs(edge.x), std::abs(edge.y)), 1.0f, 1e-3f));
        }
    }

    TEST_CASE("a light shining straight down or up has finite matrices")
    {
        for (const float z : { -1.0f, 1.0f })
        {
            CAPTURE(z);
            const auto shadow = fitSpotShadow({ .position = { 0.0f, 0.0f, 5.0f }, .direction = { 0.0f, 0.0f, z } });
            CHECK(finite(shadow.view));
            CHECK(finite(shadow.projection));
            const auto below = project(shadow, math::Float3{ 0.0f, 0.0f, 5.0f + 3.0f * z });
            CHECK(near(below.x, 0.0f));
            CHECK(near(below.y, 0.0f));
        }
    }

    TEST_CASE("a cone wider than the widest view is shadowed within it")
    {
        const FrameScene::SpotLight light{ .direction = { 0.0f, 1.0f, 0.0f }, .range = 10.0f, .outerConeAngle = math::radians(89.0f) };
        const auto shadow = fitSpotShadow(light);
        CHECK(near(1.0f / shadow.projection[0, 0], std::tan(maxSpotShadowFov / 2.0f)));
        CHECK(near(-1.0f / shadow.projection[1, 1], std::tan(maxSpotShadowFov / 2.0f)));
    }

    TEST_CASE("the near plane is a thousandth of the range, at least 5 cm")
    {
        // A point on the axis at the near plane has depth 0.
        for (const auto [range, nearPlane] : { std::pair{ 20.0f, 0.05f }, { 200.0f, 0.2f } })
        {
            CAPTURE(range);
            const FrameScene::SpotLight light{ .direction = { 0.0f, 1.0f, 0.0f }, .range = range };
            CHECK(near(project(fitSpotShadow(light), math::Float3{ 0.0f, nearPlane, 0.0f }).z, 0.0f));
        }
    }
}
