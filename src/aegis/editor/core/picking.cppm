export module aegis.editor.core:picking;
import std;
import aegis.math;
import aegis.world;

export namespace aegis::editor
{
/// @brief A half line from 'origin' along the unit vector 'direction'.
struct Ray
{
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 1.0f, 0.0f };
};

/// @brief A sphere in world space; what a click picks an entity by.
struct Sphere
{
    glm::vec3 center{ 0.0f };
    float radius{ 0.0f };
};

/// @brief An entity's bounding sphere, a candidate for a click in the viewport.
struct PickTarget
{
    StableId id;
    Sphere bounds;
};

/// @brief An entity's icon in the viewport (a light, a camera), in pixels.
struct PickIcon
{
    StableId id;
    glm::vec2 position{ 0.0f };
};

/// @brief 'local' (in a mesh's space) moved into world space by 'transform': the radius grows with the largest
///        axis scale, so the sphere still holds the mesh.
[[nodiscard]] auto transformSphere(const Sphere& local, const glm::mat4& transform) -> Sphere;

/// @brief The distance along 'ray' at which it enters 'sphere'; 0 if it starts inside, none if it misses.
[[nodiscard]] auto intersect(const Ray& ray, const Sphere& sphere) -> std::optional<float>;

/// @brief The target whose sphere 'ray' enters first, or none.
[[nodiscard]] auto pickNearest(const Ray& ray, std::span<const PickTarget> targets) -> std::optional<StableId>;

/// @brief The icon closest to 'cursor' within 'radius' pixels, or none.
[[nodiscard]] auto pickIcon(const glm::vec2& cursor, std::span<const PickIcon> icons, float radius) -> std::optional<StableId>;
}
