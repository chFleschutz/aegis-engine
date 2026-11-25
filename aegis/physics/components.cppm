export module aegis.physics:components;

import std;
import aegis.math;
import aegis.reflect;

export namespace aegis::physics
{
/// @brief How a body moves
enum class MotionType : std::uint8_t
{
    Static,    ///< Never moves; others collide with it
    Kinematic, ///< Follows its entity's Transform (a script moves it) and pushes dynamic bodies, unaffected by them
    Dynamic    ///< Moved by gravity and collisions; the PhysicsSystem writes its pose into the entity's Transform
};

/// @brief An entity's body in a playing world's physics. It needs a Collider for its shape; without one the entity has
///        no body (the PhysicsSystem warns at begin).
struct RigidBody
{
    MotionType motionType{ MotionType::Dynamic };
    /// @brief In kilograms; only a dynamic body uses it
    float mass{ 1.0f };
    float friction{ 0.5f };
    /// @brief Bounciness: 0 keeps nothing of the speed along the contact, 1 keeps all of it
    float restitution{ 0.0f };
    float linearDamping{ 0.05f };
    float angularDamping{ 0.05f };
    /// @brief 0 floats, 1 falls with the world's gravity
    float gravityFactor{ 1.0f };
};

/// @brief A box, by half its size along each axis
struct BoxShape
{
    math::Float3 halfExtents{ 0.5f };
};

struct SphereShape
{
    float radius{ 0.5f };
};

/// @brief A cylinder with half-sphere caps, standing along the entity's up axis (math::world::up)
struct CapsuleShape
{
    float radius{ 0.5f };
    /// @brief Half the length of the cylinder between the caps; the whole capsule is 2 * (halfHeight + radius) tall
    float halfHeight{ 0.5f };
};

/// @brief The shapes a Collider can have; a new shape is one more alternative (and its case in the PhysicsSystem)
using ColliderShape = std::variant<BoxShape, SphereShape, CapsuleShape>;

/// @brief Names the alternatives of ColliderShape for the editor and the scene file (Collider's 'shape' field)
enum class ShapeType : std::uint8_t
{
    Box,
    Sphere,
    Capsule
};

/// @brief The shape an entity collides with, at an offset from its location. The entity's global scale scales it.
///        An entity with a Collider and no RigidBody is a static body.
struct Collider
{
    ColliderShape shape; // A BoxShape, the first alternative
    /// @brief The shape's center in the entity's space (scaled with it)
    math::Float3 offset{ 0.0f };
};

/// @brief Walks its entity through a playing world as a character: an upright capsule that slides along walls, climbs
///        steps and slopes, sticks to the floor and pushes the dynamic bodies it walks into (Jolt's CharacterVirtual).
///        Each fixed step the PhysicsSystem moves it by desiredVelocity and gravity and writes its location, blended by
///        alpha, into the entity's Transform; it leaves the Transform's rotation and scale alone.
/// @note The entity's location is the capsule's bottom, the character's feet. The capsule stands along the up axis
///       (math::world::up) whatever the entity's rotation and scale, which don't change it. A Collider or RigidBody on
///       the same entity does nothing (the PhysicsSystem warns). Bodies, ray casts and overlaps meet the character
///       through an inner body of its shape, which carries its entity.
struct CharacterController
{
    /// @brief The capsule's radius in meters
    float radius{ 0.3f };
    /// @brief The capsule's whole height in meters, feet to top: more than twice the radius
    float height{ 1.8f };
    /// @brief The steepest slope it walks up, in degrees from level; steeper ground counts as a wall
    float maxSlope{ 50.0f };
    /// @brief The highest step it climbs, and how far down it follows the floor, in meters
    float stepHeight{ 0.3f };
    /// @brief The most force it pushes a dynamic body with, in newtons
    float pushStrength{ 100.0f };

    /// @brief The velocity a script asks for in m/s, read each fixed step (not saved). The part across the up axis
    ///        walks. The part along it is a jump: the take-off speed while grounded, ignored in the air, where the
    ///        character keeps its vertical speed and falls with the world's gravity. Set it every step; it stays
    ///        until changed, so a jump that should happen once is cleared after the step.
    math::Float3 desiredVelocity{ 0.0f };
    /// @brief Whether it stands on ground no steeper than maxSlope after the last fixed step (the PhysicsSystem fills
    ///        it; not saved)
    bool grounded{ false };
};

/// @brief Registers RigidBody, Collider and CharacterController (see world::registerComponent), so a scene file saves
///        them and the editor shows them; CharacterController's desiredVelocity and grounded are not saved.
/// @note Collider's shape is saved as the fields 'shape' (its ShapeType), 'halfExtents', 'radius' and 'halfHeight'. Each
///       reads and writes the active shape; one the shape lacks reads as that shape's default and ignores writes.
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>;
}

export template<>
struct aegis::reflect::EnumNames<aegis::physics::MotionType>
{
    static constexpr auto values = std::to_array<std::pair<std::string_view, aegis::physics::MotionType>>({
        { "Static", aegis::physics::MotionType::Static },
        { "Kinematic", aegis::physics::MotionType::Kinematic },
        { "Dynamic", aegis::physics::MotionType::Dynamic },
    });
};

export template<>
struct aegis::reflect::EnumNames<aegis::physics::ShapeType>
{
    static constexpr auto values = std::to_array<std::pair<std::string_view, aegis::physics::ShapeType>>({
        { "Box", aegis::physics::ShapeType::Box },
        { "Sphere", aegis::physics::ShapeType::Sphere },
        { "Capsule", aegis::physics::ShapeType::Capsule },
    });
};
