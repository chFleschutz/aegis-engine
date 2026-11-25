module aegis.physics;

import std;
import aegis.math;
import aegis.reflect;
import aegis.world;

namespace aegis::physics
{
namespace
{
auto shapeType(const Collider& collider) -> ShapeType
{
    return static_cast<ShapeType>(collider.shape.index());
}

void setShapeType(Collider& collider, ShapeType type)
{
    if (shapeType(collider) == type)
        return;
    switch (type)
    {
    case ShapeType::Box: collider.shape = BoxShape{}; break;
    case ShapeType::Sphere: collider.shape = SphereShape{}; break;
    case ShapeType::Capsule: collider.shape = CapsuleShape{}; break;
    }
}

auto halfExtents(const Collider& collider) -> math::Float3
{
    const auto* box = std::get_if<BoxShape>(&collider.shape);
    return box ? box->halfExtents : BoxShape{}.halfExtents;
}

void setHalfExtents(Collider& collider, math::Float3 value)
{
    if (auto* box = std::get_if<BoxShape>(&collider.shape))
        box->halfExtents = value;
}

auto radius(const Collider& collider) -> float
{
    if (const auto* sphere = std::get_if<SphereShape>(&collider.shape))
        return sphere->radius;
    if (const auto* capsule = std::get_if<CapsuleShape>(&collider.shape))
        return capsule->radius;
    return SphereShape{}.radius;
}

void setRadius(Collider& collider, float value)
{
    if (auto* sphere = std::get_if<SphereShape>(&collider.shape))
        sphere->radius = value;
    else if (auto* capsule = std::get_if<CapsuleShape>(&collider.shape))
        capsule->radius = value;
}

auto halfHeight(const Collider& collider) -> float
{
    const auto* capsule = std::get_if<CapsuleShape>(&collider.shape);
    return capsule ? capsule->halfHeight : CapsuleShape{}.halfHeight;
}

void setHalfHeight(Collider& collider, float value)
{
    if (auto* capsule = std::get_if<CapsuleShape>(&collider.shape))
        capsule->halfHeight = value;
}

static_assert(std::variant_size_v<ColliderShape> == 3, "name the new shape in ShapeType, setShapeType and the fields");
}

auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>
{
    auto body = world::registerComponent<RigidBody>(types, "RigidBody");
    if (!body)
        return std::unexpected{ body.error() };
    body->displayName("Rigid Body")
        .category("Physics")
        .field("motionType", &RigidBody::motionType, { .tooltip = "Static never moves, Kinematic follows its Transform, Dynamic falls and collides" })
        .field("mass", &RigidBody::mass, { .min = 0.001, .step = 0.1, .unit = "kg", .tooltip = "Only a dynamic body uses it" })
        .field("friction", &RigidBody::friction, { .min = 0.0, .step = 0.05 })
        .field("restitution", &RigidBody::restitution, { .min = 0.0, .max = 1.0, .step = 0.05, .tooltip = "Bounciness: 1 keeps all the speed along the contact" })
        .field("linearDamping", &RigidBody::linearDamping, { .min = 0.0, .step = 0.01 })
        .field("angularDamping", &RigidBody::angularDamping, { .min = 0.0, .step = 0.01 })
        .field("gravityFactor", &RigidBody::gravityFactor, { .step = 0.05, .tooltip = "0 floats, 1 falls with the world's gravity" });
    if (auto status = body->status(); !status)
        return status;

    auto collider = world::registerComponent<Collider>(types, "Collider");
    if (!collider)
        return std::unexpected{ collider.error() };
    // 'shape' first: the scene loader sets the fields in this order, and the others write the shape it picks.
    collider->category("Physics")
        .property("shape", shapeType, setShapeType)
        .property("halfExtents", halfExtents, setHalfExtents, { .min = 0.0, .step = 0.05, .unit = "m", .tooltip = "Box only: half the size along each axis" })
        .property("radius", radius, setRadius, { .min = 0.0, .step = 0.05, .unit = "m", .tooltip = "Sphere and capsule only" })
        .property("halfHeight", halfHeight, setHalfHeight, { .min = 0.0, .step = 0.05, .unit = "m", .tooltip = "Capsule only: half the length between its caps" })
        .field("offset", &Collider::offset, { .step = 0.05, .unit = "m", .tooltip = "The shape's center in the entity's space" });
    if (auto status = collider->status(); !status)
        return status;

    // desiredVelocity and grounded are runtime state: a script and the PhysicsSystem write them while the world plays.
    auto character = world::registerComponent<CharacterController>(types, "CharacterController");
    if (!character)
        return std::unexpected{ character.error() };
    character->displayName("Character Controller")
        .category("Physics")
        .field("radius", &CharacterController::radius, { .min = 0.01, .step = 0.05, .unit = "m" })
        .field("height", &CharacterController::height, { .min = 0.03, .step = 0.05, .unit = "m", .tooltip = "Feet to top: more than twice the radius" })
        .field("maxSlope", &CharacterController::maxSlope, { .min = 0.0, .max = 90.0, .step = 1.0, .unit = "deg", .tooltip = "The steepest slope it walks up" })
        .field("stepHeight", &CharacterController::stepHeight, { .min = 0.0, .step = 0.05, .unit = "m", .tooltip = "The highest step it climbs" })
        .field("pushStrength", &CharacterController::pushStrength, { .min = 0.0, .step = 10.0, .unit = "N", .tooltip = "The most force it pushes a dynamic body with" });
    return character->status();
}
}
