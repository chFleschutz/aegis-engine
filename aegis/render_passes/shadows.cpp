module aegis.render_passes;
import std;
import aegis.math;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// The view-space point of the camera's frustum at the NDC position (x, y, depth).
auto unproject(const math::Float4x4& inverseProjection, float x, float y, float depth) -> math::Float3
{
    const auto point = inverseProjection * math::Float4{ x, y, depth, 1.0f };
    return math::Float3{ point } / point.w;
}

// The view-space corners of the camera's frustum between 'near' and 'far' meters along its axis (clamped to its near
// and far planes): the near corners, then the far ones.
auto frustumSlice(const math::Float4x4& projection, float near, float far) -> std::array<math::Float3, 8>
{
    const auto inverseProjection = math::inverse(projection);
    std::array<math::Float3, 8> corners;
    std::size_t i = 0;
    for (const auto [x, y] : { std::pair{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } })
    {
        // Along each edge from the near plane (depth 0) to the far plane (depth 1), to the points 'near' and 'far' deep.
        const auto nearPlane = unproject(inverseProjection, x, y, 0.0f);
        const auto farPlane = unproject(inverseProjection, x, y, 1.0f);
        const auto along = [&](float distance) {
            const float t = std::clamp((distance + nearPlane.z) / (nearPlane.z - farPlane.z), 0.0f, 1.0f);
            return nearPlane + (farPlane - nearPlane) * t;
        };
        corners[i] = along(near);
        corners[i + 4] = along(far);
        ++i;
    }
    return corners;
}
}

auto spotShadowViewName(std::uint32_t index) -> std::string
{
    return std::format("SpotShadow{}", index);
}

auto spotShadowTile(std::uint32_t index) -> rhi::Rect2D
{
    const auto perRow = spotShadowAtlasExtent.x / spotShadowTileExtent.x;
    return {
        .x = (index % perRow) * spotShadowTileExtent.x,
        .y = (index / perRow) * spotShadowTileExtent.y,
        .extent = spotShadowTileExtent,
    };
}

auto shadowViewName(std::string_view view, std::uint32_t cascade) -> std::string
{
    return std::format("{}Shadow{}", view, cascade);
}

auto shadowCascadeSplits(const FrameScene::DirectionalLight& light, const math::Float4x4& projection) -> std::vector<float>
{
    const auto inverseProjection = math::inverse(projection);
    const float nearPlane = std::max(-unproject(inverseProjection, 0.0f, 0.0f, 0.0f).z, 1e-4f);
    const float farPlane = -unproject(inverseProjection, 0.0f, 0.0f, 1.0f).z;
    const float far = std::max(std::min(light.shadowDistance, farPlane), nearPlane * 1.001f);
    const float lambda = std::clamp(light.shadowSplitLambda, 0.0f, 1.0f);
    const auto count = std::clamp(light.shadowCascades, 1u, maxShadowCascades);

    std::vector<float> splits(count);
    for (std::uint32_t i = 1; i < count; ++i)
    {
        const float fraction = static_cast<float>(i) / static_cast<float>(count);
        const float logarithmic = nearPlane * std::pow(far / nearPlane, fraction);
        const float uniform = nearPlane + (far - nearPlane) * fraction;
        splits[i - 1] = lambda * logarithmic + (1.0f - lambda) * uniform;
    }
    splits.back() = far;
    return splits;
}

auto fitDirectionalShadow(const FrameScene::DirectionalLight& light, const FrameScene::Camera& camera, float near, float far,
    rhi::Extent2D extent) -> FrameScene::Camera
{
    // The bounding sphere of the slice in view space, where it depends on the projection alone.
    const auto corners = frustumSlice(camera.projection, near, far);
    math::Float3 center{ 0.0f };
    for (const auto& corner : corners)
        center = center + corner;
    center = center / static_cast<float>(corners.size());
    float radius{ 0.0f };
    for (const auto& corner : corners)
        radius = std::max(radius, math::length(corner - center));
    radius = std::max(radius, 0.01f);
    const auto worldCenter = math::Float3{ math::inverse(camera.view) * math::Float4{ center, 1.0f } };

    // The light's axes: it looks along -direction; up is the world's up unless the light shines (nearly) along it.
    const auto toLight = math::normalize(light.direction);
    const auto forward = -toLight;
    const auto worldUp = std::abs(forward.z) > 0.99f ? math::world::forward<float> : math::world::up<float>;
    const auto side = math::normalize(math::cross(forward, worldUp));
    const auto up = math::cross(side, forward);

    // The sphere's center in the light's axes, snapped down to whole texels across the map. Snapping moves the map up to
    // a texel from the center, so it reaches a texel further than the sphere: halfWidth = radius + texel, with
    // texel = 2 * halfWidth / extent.
    const float texels = static_cast<float>(std::max(extent.x, 3u));
    const float halfWidth = radius * texels / (texels - 2.0f);
    const float texel = 2.0f * halfWidth / texels;
    const auto snap = [&](float value) { return std::floor(value / texel) * texel; };
    const float x = snap(math::dot(side, worldCenter));
    const float y = snap(math::dot(up, worldCenter));
    const float eye = math::dot(toLight, worldCenter) + radius + shadowCasterReach;

    const auto view = math::Float4x4::fromRows(
        math::Float4{ side, -x },
        math::Float4{ up, -y },
        math::Float4{ toLight, -eye },
        math::Float4{ 0.0f, 0.0f, 0.0f, 1.0f });

    // Right-handed, depth 0 at the eye to 1 past the sphere, y flipped for Vulkan as the cameras' projections are.
    const float depth = 2.0f * radius + shadowCasterReach;
    const auto projection = math::orthographic(halfWidth, halfWidth, 0.0f, depth);

    return {
        .view = view,
        .projection = projection,
        .position = side * x + up * y + toLight * eye,
    };
}

auto selectShadowedSpotLights(const FrameScene& scene, const math::Float3& from, std::uint32_t budget) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> casting;
    for (std::uint32_t i = 0; i < scene.spotLights.size(); ++i)
    {
        if (scene.spotLights[i].castsShadows)
            casting.push_back(i);
    }
    const auto distance = [&](std::uint32_t i) {
        const auto offset = scene.spotLights[i].position - from;
        return math::dot(offset, offset);
    };
    std::ranges::stable_sort(casting, std::less{}, distance);
    if (casting.size() > budget)
        casting.resize(budget);
    return casting;
}

auto fitSpotShadow(const FrameScene::SpotLight& light) -> FrameScene::Camera
{
    // The light's axes: it looks along its direction; up is the world's up unless it shines (nearly) along it.
    const auto forward = math::normalize(light.direction);
    const auto worldUp = std::abs(math::dot(forward, math::world::up<float>)) > 0.99f ? math::world::forward<float> : math::world::up<float>;
    const auto side = math::normalize(math::cross(forward, worldUp));
    const auto up = math::cross(side, forward);

    const auto view = math::Float4x4::fromRows(
        math::Float4{ side, -math::dot(side, light.position) },
        math::Float4{ up, -math::dot(up, light.position) },
        math::Float4{ -forward, math::dot(forward, light.position) },
        math::Float4{ 0.0f, 0.0f, 0.0f, 1.0f });

    // Right-handed, depth 0 at the near plane to 1 at the range, y flipped for Vulkan as the cameras' projections are.
    const float fov = std::clamp(2.0f * light.outerConeAngle, 0.01f, maxSpotShadowFov);
    const float near = std::max(light.range / 1000.0f, 0.05f);
    const float far = std::max(light.range, 2.0f * near);
    const auto projection = math::perspective(fov, 1.0f, near, far);

    return {
        .view = view,
        .projection = projection,
        .position = light.position,
    };
}

auto addShadowViews(FrameScene& scene) -> void
{
    const auto mainViews = static_cast<std::uint32_t>(scene.views.size());
    for (std::uint32_t i = 0; i < mainViews; ++i)
    {
        if (scene.views[i].kind != ViewKind::Main || scene.views[i].shadow)
            continue;
        std::vector<float> splits;
        if (scene.directionalLight && scene.views[i].camera)
            splits = shadowCascadeSplits(*scene.directionalLight, scene.views[i].camera->projection);
        scene.views[i].shadow = static_cast<std::uint32_t>(scene.views.size());
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
        {
            std::optional<FrameScene::Camera> camera;
            if (cascade < splits.size())
            {
                const float near = cascade == 0 ? 0.0f : splits[cascade - 1];
                camera = fitDirectionalShadow(*scene.directionalLight, *scene.views[i].camera, near, splits[cascade]);
            }
            scene.views.push_back({
                .name = shadowViewName(scene.views[i].name, cascade),
                .camera = camera,
                .extent = shadowMapExtent,
                .kind = ViewKind::Shadow,
            });
        }
    }

    // The spot shadow views, once: all main views share them, as they share the light records.
    if (std::ranges::any_of(scene.views, [](const FrameScene::View& view) { return view.tile.has_value(); }))
        return;
    const auto mainView = std::ranges::find_if(scene.views, [](const FrameScene::View& view) { return view.kind == ViewKind::Main; });
    std::vector<std::uint32_t> selected;
    if (mainView != scene.views.end() && mainView->camera)
        selected = selectShadowedSpotLights(scene, mainView->camera->position);
    for (std::uint32_t index = 0; index < maxShadowedSpotLights; ++index)
    {
        std::optional<FrameScene::Camera> camera;
        if (index < selected.size())
        {
            auto& light = scene.spotLights[selected[index]];
            camera = fitSpotShadow(light);
            light.shadowView = static_cast<std::uint32_t>(scene.views.size());
        }
        scene.views.push_back({
            .name = spotShadowViewName(index),
            .camera = camera,
            .extent = spotShadowTileExtent,
            .kind = ViewKind::Shadow,
            .tile = spotShadowTile(index),
        });
    }
}
}
