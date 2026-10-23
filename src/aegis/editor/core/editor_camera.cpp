module aegis.editor.core;
import std;
import aegis.math;

namespace aegis::editor
{
namespace
{
// Short of straight up or down, where the view's right axis is undefined.
constexpr float maxPitch{ glm::radians(89.0f) };
// Each wheel step dollies this share of the distance to the pivot, or scales the fly speed by its inverse.
constexpr float wheelStep{ 0.1f };
}

EditorCamera::EditorCamera()
{
    // Behind and above the origin, looking at it
    lookAlong(glm::vec3{ 0.0f, -10.0f, 5.0f }, glm::vec3{ 0.0f, 10.0f, -5.0f }, glm::length(glm::vec3{ 0.0f, 10.0f, 5.0f }));
}

void EditorCamera::lookAlong(const glm::vec3& position, const glm::vec3& forward, float distance)
{
    m_position = position;
    m_distance = std::max(distance, minDistance);
    if (glm::length(forward) <= 0.0f)
        return;
    const glm::vec3 direction = glm::normalize(forward);
    m_yaw = std::atan2(-direction.x, direction.y);
    m_pitch = std::clamp(std::asin(std::clamp(direction.z, -1.0f, 1.0f)), -maxPitch, maxPitch);
}

void EditorCamera::focus(const glm::vec3& center, float radius)
{
    // The sphere touches the view's top and bottom at this distance; a little more leaves a margin
    const float fitted = std::max(radius, minDistance) / std::sin(fov / 2.0f);
    m_distance = std::max(fitted * 1.2f, minDistance);
    m_position = center - forward() * m_distance;
}

void EditorCamera::update(const CameraInput& input, float deltaSeconds)
{
    if (input.fly)
    {
        m_yaw -= input.cursorDelta.x * turnRate;
        m_pitch = std::clamp(m_pitch - input.cursorDelta.y * turnRate, -maxPitch, maxPitch);
        if (input.wheel != 0.0f)
            m_speed *= std::pow(1.0f / (1.0f - wheelStep), input.wheel);
        const glm::vec3 move = right() * input.move.x + forward() * input.move.y + math::world::UP * input.move.z;
        if (glm::length(move) > 0.0f)
            m_position += glm::normalize(move) * m_speed * deltaSeconds;
        return; // The pivot moves along
    }

    if (input.orbit)
    {
        const glm::vec3 center = pivot();
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
        const glm::vec3 center = pivot();
        m_distance = std::max(m_distance * std::pow(1.0f - wheelStep, input.wheel), minDistance);
        m_position = center - forward() * m_distance;
    }
}

auto EditorCamera::forward() const -> glm::vec3
{
    return { -std::sin(m_yaw) * std::cos(m_pitch), std::cos(m_yaw) * std::cos(m_pitch), std::sin(m_pitch) };
}

auto EditorCamera::right() const -> glm::vec3
{
    return glm::normalize(glm::cross(forward(), math::world::UP));
}

auto EditorCamera::up() const -> glm::vec3
{
    return glm::cross(right(), forward());
}

auto EditorCamera::view() const -> glm::mat4
{
    return math::viewMatrix(m_position, forward());
}

auto EditorCamera::projection(float aspect) const -> glm::mat4
{
    // As CameraSystem builds it: right handed, depth [0, 1], y negated for Vulkan
    const float tanHalfFov = std::tan(fov / 2.0f);
    glm::mat4 perspective{ 0.0f };
    perspective[0][0] = 1.0f / (aspect * tanHalfFov);
    perspective[1][1] = -1.0f / tanHalfFov;
    perspective[2][2] = far / (near - far);
    perspective[2][3] = -1.0f;
    perspective[3][2] = -(far * near) / (far - near);
    return perspective;
}

auto EditorCamera::ray(const glm::vec2& ndc, float aspect) const -> Ray
{
    const float tanHalfFov = std::tan(fov / 2.0f);
    const glm::vec3 direction = forward() + right() * (ndc.x * tanHalfFov * aspect) + up() * (ndc.y * tanHalfFov);
    return { .origin = m_position, .direction = glm::normalize(direction) };
}

auto EditorCamera::project(const glm::vec3& point, float aspect) const -> std::optional<glm::vec2>
{
    const glm::vec3 offset = point - m_position;
    const float depth = glm::dot(offset, forward());
    if (depth <= near)
        return std::nullopt;
    const float tanHalfFov = std::tan(fov / 2.0f);
    return glm::vec2{ glm::dot(offset, right()) / (depth * tanHalfFov * aspect), glm::dot(offset, up()) / (depth * tanHalfFov) };
}
}
