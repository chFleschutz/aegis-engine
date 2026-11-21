module aegis.render_passes;
import std;
import aegis.math;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// The view-space point of the camera's frustum at the NDC position (x, y, depth).
auto unproject(const glm::mat4& inverseProjection, float x, float y, float depth) -> glm::vec3
{
    const auto point = inverseProjection * glm::vec4{ x, y, depth, 1.0f };
    return glm::vec3{ point } / point.w;
}

// The view-space corners of the camera's frustum between 'near' and 'far' meters along its axis (clamped to its near
// and far planes): the near corners, then the far ones.
auto frustumSlice(const glm::mat4& projection, float near, float far) -> std::array<glm::vec3, 8>
{
    const auto inverseProjection = glm::inverse(projection);
    std::array<glm::vec3, 8> corners;
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

auto shadowViewName(std::string_view view, std::uint32_t cascade) -> std::string
{
    return std::format("{}Shadow{}", view, cascade);
}

auto shadowCascadeSplits(const FrameScene::DirectionalLight& light, const glm::mat4& projection) -> std::vector<float>
{
    const auto inverseProjection = glm::inverse(projection);
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
    glm::vec3 center{ 0.0f };
    for (const auto& corner : corners)
        center = center + corner;
    center = center / static_cast<float>(corners.size());
    float radius{ 0.0f };
    for (const auto& corner : corners)
        radius = std::max(radius, glm::length(corner - center));
    radius = std::max(radius, 0.01f);
    const auto worldCenter = glm::vec3{ glm::inverse(camera.view) * glm::vec4{ center, 1.0f } };

    // The light's axes: it looks along -direction; up is the world's up unless the light shines (nearly) along it.
    const auto toLight = glm::normalize(light.direction);
    const auto forward = -toLight;
    const auto worldUp = std::abs(forward.z) > 0.99f ? math::world::FORWARD : math::world::UP;
    const auto side = glm::normalize(glm::cross(forward, worldUp));
    const auto up = glm::cross(side, forward);

    // The sphere's center in the light's axes, snapped down to whole texels across the map. Snapping moves the map up to
    // a texel from the center, so it reaches a texel further than the sphere: halfWidth = radius + texel, with
    // texel = 2 * halfWidth / extent.
    const float texels = static_cast<float>(std::max(extent.x, 3u));
    const float halfWidth = radius * texels / (texels - 2.0f);
    const float texel = 2.0f * halfWidth / texels;
    const auto snap = [&](float value) { return std::floor(value / texel) * texel; };
    const float x = snap(glm::dot(side, worldCenter));
    const float y = snap(glm::dot(up, worldCenter));
    const float eye = glm::dot(toLight, worldCenter) + radius + shadowCasterReach;

    glm::mat4 view{ 1.0f };
    view[0][0] = side.x;
    view[1][0] = side.y;
    view[2][0] = side.z;
    view[0][1] = up.x;
    view[1][1] = up.y;
    view[2][1] = up.z;
    view[0][2] = toLight.x;
    view[1][2] = toLight.y;
    view[2][2] = toLight.z;
    view[3][0] = -x;
    view[3][1] = -y;
    view[3][2] = -eye;

    // Right-handed, depth 0 at the eye to 1 past the sphere, y flipped for Vulkan as the cameras' projections are.
    const float depth = 2.0f * radius + shadowCasterReach;
    glm::mat4 projection{ 1.0f };
    projection[0][0] = 1.0f / halfWidth;
    projection[1][1] = -1.0f / halfWidth;
    projection[2][2] = -1.0f / depth;
    projection[3][2] = 0.0f;

    return {
        .view = view,
        .projection = projection,
        .position = side * x + up * y + toLight * eye,
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
}
}
