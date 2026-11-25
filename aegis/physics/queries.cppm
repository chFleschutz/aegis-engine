export module aegis.physics:queries;

import std;
import aegis.math;
import aegis.world;

export namespace aegis::physics
{
/// @brief Where a ray cast hit a body
struct RayHit
{
    /// @brief The body's entity (a character's for its inner body)
    world::Entity entity;
    /// @brief Where the ray hit, in world space
    math::Float3 point;
    /// @brief The surface's normal at the point, unit length and pointing out of the body
    math::Float3 normal;
    /// @brief From the origin to the point, in meters
    float distance;
};

/// @brief The first body the ray from 'origin' along 'direction' hits within 'maxDistance' meters
/// @param registry A playing world's registry: its context holds the PhysicsSystem
/// @param direction Need not be unit length; a zero direction hits nothing
/// @param ignore An entity whose body the ray passes through (the caller's own); null ignores none
/// @return None without a hit, before the world begins, or without a PhysicsSystem in the registry's context
/// @note A ray that starts inside a body hits it at distance 0.
[[nodiscard]] auto raycast(const world::Registry& registry, const math::Float3& origin, const math::Float3& direction, float maxDistance, world::Entity ignore = {})
    -> std::optional<RayHit>;

/// @brief The entities whose bodies overlap the sphere, ordered by entity, each once
/// @param ignore An entity left out of the result (the caller's own); null leaves out none
/// @return None before the world begins, or without a PhysicsSystem in the registry's context
[[nodiscard]] auto overlapSphere(const world::Registry& registry, const math::Float3& center, float radius, world::Entity ignore = {}) -> std::vector<world::Entity>;

/// @brief The entities whose bodies overlap the box (half its size along each axis, turned by 'rotation'), ordered by
///        entity, each once
/// @param ignore An entity left out of the result (the caller's own); null leaves out none
/// @return None before the world begins, or without a PhysicsSystem in the registry's context
[[nodiscard]] auto overlapBox(const world::Registry& registry, const math::Float3& center, const math::Float3& halfExtents, const math::Quat& rotation,
    world::Entity ignore = {}) -> std::vector<world::Entity>;
}
