module aegis.editor.core;
import std;
import aegis.math;

namespace aegis::editor
{
auto transformSphere(const Sphere& local, const glm::mat4& transform) -> Sphere
{
    const float scale = std::max({ glm::length(glm::vec3{ transform[0] }), glm::length(glm::vec3{ transform[1] }), glm::length(glm::vec3{ transform[2] }) });
    return { .center = glm::vec3{ transform * glm::vec4{ local.center, 1.0f } }, .radius = local.radius * scale };
}

auto intersect(const Ray& ray, const Sphere& sphere) -> std::optional<float>
{
    // |origin + t * direction - center|^2 = radius^2, with a unit direction
    const glm::vec3 offset = ray.origin - sphere.center;
    const float b = glm::dot(offset, ray.direction);
    const float c = glm::dot(offset, offset) - sphere.radius * sphere.radius;
    if (c <= 0.0f)
        return 0.0f; // Inside
    if (b > 0.0f)
        return std::nullopt; // Outside and pointing away
    const float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return std::nullopt;
    return -b - std::sqrt(discriminant);
}

auto pickNearest(const Ray& ray, std::span<const PickTarget> targets) -> std::optional<StableId>
{
    std::optional<StableId> nearest;
    float nearestDistance = std::numeric_limits<float>::max();
    for (const auto& target : targets)
    {
        const auto distance = intersect(ray, target.bounds);
        if (distance && *distance < nearestDistance)
        {
            nearestDistance = *distance;
            nearest = target.id;
        }
    }
    return nearest;
}

auto pickIcon(const glm::vec2& cursor, std::span<const PickIcon> icons) -> std::optional<StableId>
{
    std::optional<StableId> nearest;
    float nearestDistance = std::numeric_limits<float>::max();
    for (const auto& icon : icons)
    {
        const float distance = glm::length(icon.position - cursor);
        if (distance <= icon.radius && distance <= nearestDistance)
        {
            nearestDistance = distance;
            nearest = icon.id;
        }
    }
    return nearest;
}
}
