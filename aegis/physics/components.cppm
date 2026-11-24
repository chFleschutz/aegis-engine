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

/// @brief Registers RigidBody and Collider (see world::registerComponent), so a scene file saves them and the editor
///        shows them.
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
