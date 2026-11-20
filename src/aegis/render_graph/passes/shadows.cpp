module aegis.render_passes;
import std;
import aegis.math;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// The view-space corners of the camera's frustum between its near plane and 'distance' meters along its axis (as far as
// its far plane): the near corners, then the far ones.
auto frustumSlice(const glm::mat4& projection, float distance) -> std::array<glm::vec3, 8>
{
    const auto inverseProjection = glm::inverse(projection);
    const auto unproject = [&](float x, float y, float depth) {
        const auto point = inverseProjection * glm::vec4{ x, y, depth, 1.0f };
        return glm::vec3{ point } / point.w;
    };

    std::array<glm::vec3, 8> corners;
    std::size_t i = 0;
    for (const auto [x, y] : { std::pair{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } })
    {
        // Along each edge from the near plane (depth 0) to the far plane (depth 1), to the point 'distance' deep.
        const auto near = unproject(x, y, 0.0f);
        const auto far = unproject(x, y, 1.0f);
        const float t = std::clamp((distance + near.z) / (near.z - far.z), 0.0f, 1.0f);
        corners[i] = near;
        corners[i + 4] = near + (far - near) * t;
        ++i;
    }
    return corners;
}
}

auto shadowViewName(std::string_view view) -> std::string
{
    return std::format("{}Shadow", view);
}

auto fitDirectionalShadow(const FrameScene::DirectionalLight& light, const FrameScene::Camera& camera, rhi::Extent2D extent)
    -> FrameScene::Camera
{
    // The bounding sphere of the slice in view space, where it depends on the projection alone.
    const auto corners = frustumSlice(camera.projection, light.shadowDistance);
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

    // The sphere's center in the light's axes, snapped to whole texels across the map.
    const float texel = 2.0f * radius / static_cast<float>(std::max(extent.x, 1u));
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
    projection[0][0] = 1.0f / radius;
    projection[1][1] = -1.0f / radius;
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
        std::optional<FrameScene::Camera> camera;
        if (scene.directionalLight && scene.views[i].camera)
            camera = fitDirectionalShadow(*scene.directionalLight, *scene.views[i].camera);
        scene.views[i].shadow = static_cast<std::uint32_t>(scene.views.size());
        scene.views.push_back({
            .name = shadowViewName(scene.views[i].name),
            .camera = camera,
            .extent = shadowMapExtent,
            .kind = ViewKind::Shadow,
        });
    }
}
}
