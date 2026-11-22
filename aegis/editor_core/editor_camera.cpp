module aegis.editor.core;
import std;
import aegis.math;

namespace aegis::editor
{
namespace
{
// Short of straight up or down, where the view's right axis is undefined.
constexpr float maxPitch{ math::radians(89.0f) };
// Each wheel step dollies this share of the distance to the pivot, or scales the fly speed by its inverse.
constexpr float wheelStep{ 0.1f };
}

EditorCamera::EditorCamera()
{
    // Behind and above the origin, looking at it
    lookAlong(math::Float3{ 0.0f, -10.0f, 5.0f }, math::Float3{ 0.0f, 10.0f, -5.0f }, math::length(math::Float3{ 0.0f, 10.0f, 5.0f }));
}

void EditorCamera::lookAlong(const math::Float3& position, const math::Float3& forward, float distance)
{
    m_position = position;
    m_distance = std::max(distance, minDistance);
    if (math::length(forward) <= 0.0f)
        return;
    const math::Float3 direction = math::normalize(forward);
    m_yaw = std::atan2(-direction.x, direction.y);
    m_pitch = std::clamp(std::asin(std::clamp(direction.z, -1.0f, 1.0f)), -maxPitch, maxPitch);
}

void EditorCamera::focus(const math::Float3& center, float radius)
{
    // The sphere touches the view's top and bottom at this distance; a little more leaves a margin
    const float fitted = std::max(radius, minDistance) / std::sin(fov / 2.0f);
    m_distance = std::max(fitted * 1.2f, minDistance);
    m_position = center - forward() * m_distance;
}

void EditorCamera::orbitToward(const math::Float3& forward)
{
    if (math::length(forward) <= 0.0f)
        return;
    const math::Float3 center = pivot();
    const math::Float3 direction = math::normalize(forward);
    // Straight up or down has no heading: the current one stays
    if (math::length(math::Float2{ direction.x, direction.y }) > 1e-4f)
        m_yaw = std::atan2(-direction.x, direction.y);
    m_pitch = std::clamp(std::asin(std::clamp(direction.z, -1.0f, 1.0f)), -maxPitch, maxPitch);
    m_position = center - this->forward() * m_distance;
}

void EditorCamera::update(const CameraInput& input, float deltaSeconds)
{
    if (input.fly)
    {
        m_yaw -= input.cursorDelta.x * turnRate;
        m_pitch = std::clamp(m_pitch - input.cursorDelta.y * turnRate, -maxPitch, maxPitch);
        if (input.wheel != 0.0f)
            m_speed *= std::pow(1.0f / (1.0f - wheelStep), input.wheel);
        const math::Float3 move = right() * input.move.x + forward() * input.move.y + math::world::up<float> * input.move.z;
        if (math::length(move) > 0.0f)
            m_position += math::normalize(move) * m_speed * deltaSeconds;
        return; // The pivot moves along
    }

    if (input.orbit)
    {
        const math::Float3 center = pivot();
        m_yaw -= input.cursorDelta.x * turnRate;
        m_pitch = std::clamp(m_pitch - input.cursorDelta.y * turnRate, -maxPitch, maxPitch);
        m_position = center - forward() * m_distance;
    }
    else if (input.pan)
    {
        // The pivot follows the cursor: one pixel moves about one pixel's worth of the view at the pivot
        const float unitsPerPixel = m_distance * turnRate;
        m_position += (-right() * input.cursorDelta.x + up() * input.cursorDelta.y) * unitsPerPixel;
    }

    if (input.wheel != 0.0f)
    {
        const math::Float3 center = pivot();
        m_distance = std::max(m_distance * std::pow(1.0f - wheelStep, input.wheel), minDistance);
        m_position = center - forward() * m_distance;
    }
}

auto EditorCamera::forward() const -> math::Float3
{
    return { -std::sin(m_yaw) * std::cos(m_pitch), std::cos(m_yaw) * std::cos(m_pitch), std::sin(m_pitch) };
}

auto EditorCamera::right() const -> math::Float3
{
    return math::normalize(math::cross(forward(), math::world::up<float>));
}

auto EditorCamera::up() const -> math::Float3
{
    return math::cross(right(), forward());
}

auto EditorCamera::view() const -> math::Float4x4
{
    return math::viewMatrix(m_position, forward());
}

auto EditorCamera::projection(float aspect) const -> math::Float4x4
{
    // As CameraSystem builds it
    return math::perspective(fov, aspect, near, far);
}

auto EditorCamera::ray(const math::Float2& ndc, float aspect) const -> Ray
{
    const float tanHalfFov = std::tan(fov / 2.0f);
    const math::Float3 direction = forward() + right() * (ndc.x * tanHalfFov * aspect) + up() * (ndc.y * tanHalfFov);
    return { .origin = m_position, .direction = math::normalize(direction) };
}

auto EditorCamera::project(const math::Float3& point, float aspect) const -> std::optional<math::Float2>
{
    const math::Float3 offset = point - m_position;
    const float depth = math::dot(offset, forward());
    if (depth <= near)
        return std::nullopt;
    const float tanHalfFov = std::tan(fov / 2.0f);
    return math::Float2{ math::dot(offset, right()) / (depth * tanHalfFov * aspect), math::dot(offset, up()) / (depth * tanHalfFov) };
}

auto EditorCamera::projectSphere(const Sphere& sphere, float aspect) const -> std::optional<NdcRect>
{
    const math::Float3 offset = sphere.center - m_position;
    const float depth = math::dot(offset, forward());
    if (depth - sphere.radius <= near)
        return std::nullopt;
    // Per axis, the two lines through the eye that touch the sphere's circle in that axis' plane: their slopes
    // (offset along the axis per unit of depth) bound what the sphere covers
    const float radius2 = sphere.radius * sphere.radius;
    const float denominator = depth * depth - radius2;
    const auto slopes = [&](float along) -> math::Float2 {
        const float spread = sphere.radius * std::sqrt(along * along + denominator);
        return math::Float2{ along * depth - spread, along * depth + spread } / denominator;
    };
    const float tanHalfFov = std::tan(fov / 2.0f);
    const math::Float2 x = slopes(math::dot(offset, right())) / (tanHalfFov * aspect);
    const math::Float2 y = slopes(math::dot(offset, up())) / tanHalfFov;
    return NdcRect{ .min = { x.x, y.x }, .max = { x.y, y.y } };
}

auto navigationAxes(const EditorCamera& camera) -> std::array<NavigationAxis, 6>
{
    const math::Float3 right = camera.right();
    const math::Float3 up = camera.up();
    const math::Float3 forward = camera.forward();
    std::array<NavigationAxis, 6> axes;
    for (int axis = 0; axis < 3; ++axis)
    {
        for (const bool positive : { true, false })
        {
            math::Float3 direction{ 0.0f };
            direction[axis] = positive ? 1.0f : -1.0f;
            axes[static_cast<std::size_t>(axis * 2 + (positive ? 0 : 1))] = NavigationAxis{
                .direction = direction,
                .axis = axis,
                .positive = positive,
                .offset = { math::dot(direction, right), -math::dot(direction, up) },
                .depth = -math::dot(direction, forward),
            };
        }
    }
    std::ranges::stable_sort(axes, std::less{}, &NavigationAxis::depth);
    return axes;
}

auto pickNavigationAxis(std::span<const NavigationAxis> axes, const math::Float2& cursor, float discRadius) -> std::optional<NavigationAxis>
{
    // The nearest is last
    for (const auto& axis : axes | std::views::reverse)
    {
        if (math::length(cursor - axis.offset) <= discRadius)
            return axis;
    }
    return std::nullopt;
}
}
