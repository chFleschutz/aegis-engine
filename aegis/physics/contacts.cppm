export module aegis.physics:contacts;

import std;
import aegis.math;
import aegis.world;

export namespace aegis::physics
{
/// @brief The bodies of two entities started touching in a fixed step (a character's, too): an event (world::Events)
///        that the PhysicsSystem emits after the step, so scripts and systems read it in the next update
/// @note 'a' is the entity with the lower ID, so a pair always reads the same; check both to find yours. A pair that
///       stays in contact sends one begin, also while its bodies sleep, until a ContactEnd.
struct ContactBegin
{
    world::Entity a;
    world::Entity b;
    /// @brief Where they touch, in world space
    math::Float3 point;
    /// @brief Unit length, pointing from a toward b
    math::Float3 normal;
};

/// @brief The bodies of two entities that sent a ContactBegin stopped touching in a fixed step, or one of them lost its
///        body (its entity was destroyed, or its Collider changed or was removed): read it in the next update
/// @note An entity may be gone by then: check world::Registry::valid before using it.
struct ContactEnd
{
    world::Entity a;
    world::Entity b;
};
}
