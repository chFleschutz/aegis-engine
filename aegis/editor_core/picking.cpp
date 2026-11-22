module aegis.editor.core;
import std;
import aegis.math;

namespace aegis::editor
{
auto transformSphere(const Sphere& local, const math::Float4x4& transform) -> Sphere
{
    const float scale = std::max({ math::length(math::Float3{ transform.column(0) }), math::length(math::Float3{ transform.column(1) }),
        math::length(math::Float3{ transform.column(2) }) });
    return { .center = math::Float3{ transform * math::Float4{ local.center, 1.0f } }, .radius = local.radius * scale };
}

auto enclose(const Sphere& a, const Sphere& b) -> Sphere
{
    const math::Float3 offset = b.center - a.center;
    const float distance = math::length(offset);
    if (distance + b.radius <= a.radius)
        return a;
    if (distance + a.radius <= b.radius)
        return b;
    // From the far side of 'a' to the far side of 'b', along the line through both centers
    const float radius = (distance + a.radius + b.radius) * 0.5f;
    return { .center = a.center + offset * ((radius - a.radius) / distance), .radius = radius };
}

auto intersect(const Ray& ray, const Sphere& sphere) -> std::optional<float>
{
    // |origin + t * direction - center|^2 = radius^2, with a unit direction
    const math::Float3 offset = ray.origin - sphere.center;
    const float b = math::dot(offset, ray.direction);
    const float c = math::dot(offset, offset) - sphere.radius * sphere.radius;
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

auto pickIcon(const math::Float2& cursor, std::span<const PickIcon> icons) -> std::optional<StableId>
{
    std::optional<StableId> nearest;
    float nearestDistance = std::numeric_limits<float>::max();
    for (const auto& icon : icons)
    {
        const float distance = math::length(icon.position - cursor);
        if (distance <= icon.radius && distance <= nearestDistance)
        {
            nearestDistance = distance;
            nearest = icon.id;
        }
    }
    return nearest;
}
}
