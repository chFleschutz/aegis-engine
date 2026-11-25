module;

// Jolt.h comes first, as Jolt requires; its defines come from the Jolt target (external/CMakeLists.txt).
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>

module aegis.physics;

import std;
import aegis.core;
import aegis.math;
import aegis.world;

namespace aegis::physics
{
namespace
{
// Bodies that move collide with everything; the ones that don't only with those that move.
namespace layers
{
constexpr JPH::ObjectLayer NON_MOVING{ 0 };
constexpr JPH::ObjectLayer MOVING{ 1 };
}

namespace broad_phase_layers
{
constexpr JPH::BroadPhaseLayer NON_MOVING{ 0 };
constexpr JPH::BroadPhaseLayer MOVING{ 1 };
constexpr JPH::uint COUNT{ 2 };
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    [[nodiscard]] auto GetNumBroadPhaseLayers() const -> JPH::uint override { return broad_phase_layers::COUNT; }

    [[nodiscard]] auto GetBroadPhaseLayer(JPH::ObjectLayer layer) const -> JPH::BroadPhaseLayer override
    {
        return layer == layers::MOVING ? broad_phase_layers::MOVING : broad_phase_layers::NON_MOVING;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] auto GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const -> const char* override
    {
        return layer == broad_phase_layers::MOVING ? "Moving" : "NonMoving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    [[nodiscard]] auto ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const -> bool override
    {
        return layer == layers::MOVING || broadPhase == broad_phase_layers::MOVING;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter
{
public:
    [[nodiscard]] auto ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const -> bool override
    {
        return a == layers::MOVING || b == layers::MOVING;
    }
};

void traceJolt(const char* format, ...)
{
    std::array<char, 1024> text{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text.data(), text.size(), format, args);
    va_end(args);
    log::info("Jolt: {}", text.data());
}

#ifdef JPH_ENABLE_ASSERTS
auto joltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line) -> bool
{
    log::error("Jolt assert failed at {}:{}: {} {}", file, line, expression, message ? message : "");
    return true; // Break
}
#endif

// Jolt's process-wide state (its allocator hooks, factory and registered types), set up while any physics world exists.
std::mutex g_joltMutex;
std::uint32_t g_joltUsers{ 0 };

struct JoltUser
{
    JoltUser()
    {
        const std::scoped_lock lock{ g_joltMutex };
        if (g_joltUsers++ != 0)
            return;
        JPH::RegisterDefaultAllocator();
        JPH::Trace = traceJolt;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = joltAssertFailed;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    JoltUser(const JoltUser&) = delete;
    auto operator=(const JoltUser&) -> JoltUser& = delete;

    ~JoltUser()
    {
        const std::scoped_lock lock{ g_joltMutex };
        if (--g_joltUsers != 0)
            return;
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

auto toJolt(const math::Float3& v) -> JPH::Vec3
{
    return JPH::Vec3{ v.x, v.y, v.z };
}

auto toJoltPosition(const math::Float3& v) -> JPH::RVec3
{
    return JPH::RVec3{ v.x, v.y, v.z };
}

auto toJolt(const math::Quat& q) -> JPH::Quat
{
    return JPH::Quat{ q.x, q.y, q.z, q.w }.Normalized();
}

auto fromJolt(JPH::RVec3Arg v) -> math::Float3
{
    return math::Float3{ static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()) };
}

auto fromJolt(JPH::QuatArg q) -> math::Quat
{
    return math::Quat::fromXyzw(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

// A body's user data is its entity (a character's inner body's too), so queries and contacts name the entity.
auto toUserData(entt::entity entity) -> JPH::uint64
{
    return static_cast<JPH::uint64>(std::to_underlying(entity));
}

auto entityOf(JPH::uint64 userData) -> world::Entity
{
    return world::Entity{ static_cast<entt::entity>(static_cast<std::underlying_type_t<entt::entity>>(userData)) };
}

// Skips the bodies of one entity: its own body and a character's inner body.
class IgnoreEntityFilter final : public JPH::BodyFilter
{
public:
    explicit IgnoreEntityFilter(world::Entity ignore) :
        m_ignore{ ignore }
    {
    }

    [[nodiscard]] auto ShouldCollideLocked(const JPH::Body& body) const -> bool override
    {
        return !m_ignore || entityOf(body.GetUserData()) != m_ignore;
    }

private:
    world::Entity m_ignore;
};

// The entities of the bodies a shape overlaps, ordered by entity, each once.
auto overlappingEntities(const JPH::PhysicsSystem& physics, const JPH::Shape& shape, JPH::RMat44Arg transform, world::Entity ignore) -> std::vector<world::Entity>
{
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    const IgnoreEntityFilter filter{ ignore };
    physics.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(), transform, JPH::CollideShapeSettings{}, JPH::RVec3::sZero(), collector, {}, {}, filter);

    std::vector<world::Entity> entities;
    entities.reserve(collector.mHits.size());
    const auto& bodyInterface = physics.GetBodyInterface();
    for (const auto& hit : collector.mHits)
        entities.push_back(entityOf(bodyInterface.GetUserData(hit.mBodyID2)));
    const auto order = [](world::Entity a, world::Entity b) { return std::to_underlying(a.id()) < std::to_underlying(b.id()); };
    std::ranges::sort(entities, order);
    const auto [first, last] = std::ranges::unique(entities);
    entities.erase(first, last);
    return entities;
}

auto toJolt(MotionType type) -> JPH::EMotionType
{
    switch (type)
    {
    case MotionType::Static: return JPH::EMotionType::Static;
    case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic: return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

// The shape before the entity's scale, centered on the collider's offset, or why it can't be made.
auto unscaledShape(const Collider& collider) -> std::expected<JPH::ShapeRefC, std::string>
{
    // The cylinder of Jolt's capsule runs along its Y axis; ours stands along math::world::up (Z).
    auto rotation = JPH::Quat::sIdentity();
    const auto made = core::match(
        collider.shape,
        [](const BoxShape& box) -> std::expected<JPH::ShapeSettings::ShapeResult, std::string> {
            const auto& h = box.halfExtents;
            if (!(h.x > 0.0f && h.y > 0.0f && h.z > 0.0f))
                return std::unexpected{ std::format("a box needs half extents above zero, not ({}, {}, {})", h.x, h.y, h.z) };
            const float convexRadius = std::min({ JPH::cDefaultConvexRadius, h.x, h.y, h.z });
            const JPH::Ref<JPH::BoxShapeSettings> settings = new JPH::BoxShapeSettings{ toJolt(h), convexRadius };
            return settings->Create();
        },
        [](const SphereShape& sphere) -> std::expected<JPH::ShapeSettings::ShapeResult, std::string> {
            if (!(sphere.radius > 0.0f))
                return std::unexpected{ std::format("a sphere needs a radius above zero, not {}", sphere.radius) };
            const JPH::Ref<JPH::SphereShapeSettings> settings = new JPH::SphereShapeSettings{ sphere.radius };
            return settings->Create();
        },
        [&rotation](const CapsuleShape& capsule) -> std::expected<JPH::ShapeSettings::ShapeResult, std::string> {
            if (!(capsule.radius > 0.0f && capsule.halfHeight > 0.0f))
                return std::unexpected{
                    std::format("a capsule needs a radius and half height above zero, not {} and {}", capsule.radius, capsule.halfHeight)
                };
            rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
            const JPH::Ref<JPH::CapsuleShapeSettings> settings = new JPH::CapsuleShapeSettings{ capsule.halfHeight, capsule.radius };
            return settings->Create();
        });
    if (!made)
        return std::unexpected{ made.error() };
    if (made->HasError())
        return std::unexpected{ std::string{ made->GetError().c_str() } };

    const auto offset = toJolt(collider.offset);
    if (offset.IsNearZero() && rotation.IsClose(JPH::Quat::sIdentity()))
        return made->Get();
    const JPH::Ref<JPH::RotatedTranslatedShapeSettings> placed = new JPH::RotatedTranslatedShapeSettings{ offset, rotation, made->Get() };
    const auto result = placed->Create();
    if (result.HasError())
        return std::unexpected{ std::string{ result.GetError().c_str() } };
    return result.Get();
}

// The collider's shape scaled by the entity's global scale (made valid for the shape: a sphere scales uniformly).
auto makeShape(const Collider& collider, const math::Float3& scale) -> std::expected<JPH::ShapeRefC, std::string>
{
    auto shape = unscaledShape(collider);
    if (!shape)
        return shape;
    const auto joltScale = toJolt(scale);
    if (joltScale.IsClose(JPH::Vec3::sOne()))
        return shape;
    if (joltScale.Abs().ReduceMin() <= 1.0e-6f)
        return std::unexpected{ std::format("the entity's scale ({}, {}, {}) is zero along an axis", scale.x, scale.y, scale.z) };
    const JPH::Ref<JPH::ScaledShapeSettings> scaled = new JPH::ScaledShapeSettings{ *shape, (*shape)->MakeScaleValid(joltScale) };
    const auto result = scaled->Create();
    if (result.HasError())
        return std::unexpected{ std::string{ result.GetError().c_str() } };
    return result.Get();
}

auto parentOf(world::Registry& registry, world::Entity entity) -> world::Entity
{
    if (!registry.has<Parent>(entity))
        return {};
    const auto parent = registry.get<Parent>(entity).entity;
    return registry.valid(parent) ? parent : world::Entity{};
}

// The entity's global transform composed from the Transforms of it and its ancestors, as the TransformSystem will
// compute it this frame. Their GlobalTransforms may be older: not yet computed at begin (the TransformSystem begins
// after this system), or a frame behind a script that moved them.
auto globalOf(world::Registry& registry, world::Entity entity) -> GlobalTransform
{
    const auto& transform = registry.get<Transform>(entity);
    if (const auto parent = parentOf(registry, entity))
        return world::detail::compose(globalOf(registry, parent), transform);
    return GlobalTransform{ .matrix = transform.matrix() };
}
}

struct PhysicsSystem::Impl
{
    struct Body
    {
        JPH::BodyID id;
        MotionType motionType;
        // A dynamic body's pose before and after the last step, for the frame's blend
        math::Float3 previousLocation;
        math::Quat previousRotation;
        math::Float3 location;
        math::Quat rotation;
    };

    static constexpr JPH::uint MAX_BODIES{ 65536 };
    static constexpr JPH::uint MAX_BODY_PAIRS{ 65536 };
    static constexpr JPH::uint MAX_CONTACT_CONSTRAINTS{ 10240 };
    static constexpr std::size_t TEMP_ALLOCATOR_BYTES{ 10 * 1024 * 1024 };
    static constexpr float PENETRATION_SLOP{ 0.002f };

    JoltUser jolt; // First, so Jolt's process-wide state outlives the rest
    BroadPhaseLayers broadPhase;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    JPH::TempAllocatorImpl temp{ TEMP_ALLOCATOR_BYTES };
    JPH::JobSystemSingleThreaded jobs{ JPH::cMaxPhysicsJobs };
    JPH::PhysicsSystem physics;

    std::unordered_map<entt::entity, Body> bodies;
    std::vector<entt::entity> pending; // Entities whose Collider or RigidBody came or went since the last step
    bool running{ false };             // Between onBegin and onReset: bodies exist and follow the signals

    Impl()
    {
        physics.Init(MAX_BODIES, 0, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, broadPhase, objectVsBroadPhase, objectPairs);
        physics.SetGravity(toJolt(DEFAULT_GRAVITY));
        // Resting bodies sink into each other by up to the penetration slop (Jolt: 2 cm), which shows; 2 mm still rests
        // without jitter.
        auto settings = physics.GetPhysicsSettings();
        settings.mPenetrationSlop = PENETRATION_SLOP;
        physics.SetPhysicsSettings(settings);
    }

    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;

    ~Impl() { removeAll(); }

    void removeBody(entt::entity entity)
    {
        const auto found = bodies.find(entity);
        if (found == bodies.end())
            return;
        auto& bodyInterface = physics.GetBodyInterface();
        bodyInterface.RemoveBody(found->second.id);
        bodyInterface.DestroyBody(found->second.id);
        bodies.erase(found);
    }

    void removeAll()
    {
        auto& bodyInterface = physics.GetBodyInterface();
        for (const auto& [entity, body] : bodies)
        {
            bodyInterface.RemoveBody(body.id);
            bodyInterface.DestroyBody(body.id);
        }
        bodies.clear();
        pending.clear();
    }

    // Gives the entity a body for its Collider, replacing the one it had; none without a Collider.
    void createBody(world::Registry& registry, world::Entity entity)
    {
        removeBody(entity.id());
        if (!registry.has<Collider>(entity))
            return;

        const auto* rigidBody = registry.has<RigidBody>(entity) ? &registry.get<RigidBody>(entity) : nullptr;
        const auto motionType = rigidBody ? rigidBody->motionType : MotionType::Static;
        const auto pose = math::decomposeTRS(globalOf(registry, entity).matrix);

        auto shape = makeShape(registry.get<Collider>(entity), pose.scale);
        if (!shape)
        {
            const auto* name = registry.has<Name>(entity) ? &registry.get<Name>(entity).name : nullptr;
            log::warn("The entity '{}' has no body: {}", name ? *name : std::string{}, shape.error());
            return;
        }

        const bool moves = motionType != MotionType::Static;
        JPH::BodyCreationSettings settings{
            *shape, toJoltPosition(pose.location), toJolt(pose.rotation), toJolt(motionType), moves ? layers::MOVING : layers::NON_MOVING
        };
        settings.mUserData = toUserData(entity.id());
        if (rigidBody)
        {
            settings.mFriction = rigidBody->friction;
            settings.mRestitution = rigidBody->restitution;
            settings.mLinearDamping = rigidBody->linearDamping;
            settings.mAngularDamping = rigidBody->angularDamping;
            settings.mGravityFactor = rigidBody->gravityFactor;
            if (motionType == MotionType::Dynamic)
            {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = std::max(rigidBody->mass, 1.0e-3f);
            }
        }

        const auto id = physics.GetBodyInterface().CreateAndAddBody(settings, moves ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (id.IsInvalid())
        {
            log::warn("The physics world is full: an entity has no body ({} bodies)", MAX_BODIES);
            return;
        }
        bodies.emplace(entity.id(), Body{
                                        .id = id,
                                        .motionType = motionType,
                                        .previousLocation = pose.location,
                                        .previousRotation = pose.rotation,
                                        .location = pose.location,
                                        .rotation = pose.rotation,
                                    });
        if (moves && !registry.has<DynamicTag>(entity))
            registry.add<DynamicTag>(entity);
    }

    void applyPending(world::Registry& registry)
    {
        auto entities = std::move(pending);
        pending.clear();
        std::ranges::sort(entities);
        const auto [first, last] = std::ranges::unique(entities);
        entities.erase(first, last);
        for (const auto entity : entities)
        {
            if (registry.valid(world::Entity{ entity }))
                createBody(registry, world::Entity{ entity });
            else
                removeBody(entity);
        }
    }
};

PhysicsSystem::PhysicsSystem() :
    m_impl{ std::make_unique<Impl>() }
{
}

PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::onAttach(world::Registry& registry)
{
    registry.setContext<PhysicsSystem>(*this);
    registry.onConstruct<Collider>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onConstruct<RigidBody>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onDestroy<RigidBody>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onDestroy<Collider>().connect<&PhysicsSystem::onColliderDestroyed>(this);
}

void PhysicsSystem::onDetach(world::Registry& registry)
{
    m_impl->removeAll();
    m_impl->running = false;
    registry.onConstruct<Collider>().disconnect(this);
    registry.onConstruct<RigidBody>().disconnect(this);
    registry.onDestroy<RigidBody>().disconnect(this);
    registry.onDestroy<Collider>().disconnect(this);
    if (registry.context<PhysicsSystem>() == this)
        registry.eraseContext<PhysicsSystem>();
}

void PhysicsSystem::onBegin(world::Registry& registry)
{
    m_impl->removeAll();

    std::vector<world::Entity> entities;
    for (const auto entity : registry.view<Collider>())
        entities.emplace_back(entity);
    for (const auto entity : entities)
        m_impl->createBody(registry, entity);

    std::size_t bodiesWithoutCollider{ 0 };
    for ([[maybe_unused]] const auto entity : registry.view<RigidBody>(entt::exclude<Collider>))
        ++bodiesWithoutCollider;
    if (bodiesWithoutCollider != 0)
        log::warn("{} entities have a RigidBody and no Collider: they have no body until they get one", bodiesWithoutCollider);

    m_impl->physics.OptimizeBroadPhase();
    m_impl->running = true;
}

void PhysicsSystem::onFixedUpdate(world::Registry& registry, float stepSeconds)
{
    if (!m_impl->running)
        return;
    m_impl->applyPending(registry);

    auto& bodyInterface = m_impl->physics.GetBodyInterface();
    for (const auto& [entity, body] : m_impl->bodies)
    {
        if (body.motionType != MotionType::Kinematic)
            continue;
        const auto pose = math::decomposeTRS(globalOf(registry, world::Entity{ entity }).matrix);
        bodyInterface.MoveKinematic(body.id, toJoltPosition(pose.location), toJolt(pose.rotation), stepSeconds);
    }

    constexpr int collisionSteps{ 1 };
    if (const auto error = m_impl->physics.Update(stepSeconds, collisionSteps, &m_impl->temp, &m_impl->jobs); error != JPH::EPhysicsUpdateError::None)
        log::warn("The physics step ran out of room (Jolt error {}): some contacts were dropped", static_cast<std::uint32_t>(error));

    for (auto& [entity, body] : m_impl->bodies)
    {
        if (body.motionType != MotionType::Dynamic)
            continue;
        JPH::RVec3 location;
        JPH::Quat rotation;
        bodyInterface.GetPositionAndRotation(body.id, location, rotation);
        body.previousLocation = body.location;
        body.previousRotation = body.rotation;
        body.location = fromJolt(location);
        body.rotation = fromJolt(rotation);
    }
}

void PhysicsSystem::onUpdate(world::Registry& registry, float)
{
    if (!m_impl->running)
        return;
    const float alpha = registry.time().alpha;
    for (const auto& [id, body] : m_impl->bodies)
    {
        if (body.motionType != MotionType::Dynamic)
            continue;
        const world::Entity entity{ id };
        const auto location = math::lerp(body.previousLocation, body.location, alpha);
        const auto rotation = math::slerp(body.previousRotation, body.rotation, alpha);

        auto& transform = registry.get<Transform>(entity);
        if (const auto parent = parentOf(registry, entity))
        {
            const auto parentGlobal = globalOf(registry, parent);
            const auto local = world::detail::toLocal(parentGlobal, math::transformMatrix(location, rotation, parentGlobal.scale() * transform.scale));
            transform.location = local.location;
            transform.rotation = local.rotation;
        }
        else
        {
            transform.location = location;
            transform.rotation = rotation;
        }
    }
}

void PhysicsSystem::onReset(world::Registry&)
{
    m_impl->removeAll();
    m_impl->running = false;
}

auto PhysicsSystem::gravity() const -> math::Float3
{
    return fromJolt(JPH::RVec3{ m_impl->physics.GetGravity() });
}

void PhysicsSystem::setGravity(const math::Float3& gravity)
{
    m_impl->physics.SetGravity(toJolt(gravity));
}

auto PhysicsSystem::bodyCount() const -> std::size_t
{
    return m_impl->bodies.size();
}

auto PhysicsSystem::hasBody(world::Entity entity) const -> bool
{
    return m_impl->bodies.contains(entity.id());
}

auto PhysicsSystem::raycast(const math::Float3& origin, const math::Float3& direction, float maxDistance, world::Entity ignore) const -> std::optional<RayHit>
{
    const float directionLength = math::length(direction);
    if (!(directionLength > 0.0f) || !(maxDistance > 0.0f))
        return std::nullopt;
    const auto unit = direction / directionLength;
    const JPH::RRayCast ray{ toJoltPosition(origin), toJolt(unit * maxDistance) };

    JPH::RayCastResult result;
    const IgnoreEntityFilter filter{ ignore };
    if (!m_impl->physics.GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, filter))
        return std::nullopt;

    const JPH::BodyLockRead lock{ m_impl->physics.GetBodyLockInterface(), result.mBodyID };
    if (!lock.Succeeded())
        return std::nullopt;
    const auto& body = lock.GetBody();
    const auto point = ray.GetPointOnRay(result.mFraction);
    return RayHit{
        .entity = entityOf(body.GetUserData()),
        .point = fromJolt(point),
        .normal = fromJolt(JPH::RVec3{ body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point) }),
        .distance = result.mFraction * maxDistance,
    };
}

auto PhysicsSystem::overlapSphere(const math::Float3& center, float radius, world::Entity ignore) const -> std::vector<world::Entity>
{
    if (!(radius > 0.0f))
        return {};
    JPH::SphereShape sphere{ radius };
    sphere.SetEmbedded(); // On the stack: no reference may delete it
    return overlappingEntities(m_impl->physics, sphere, JPH::RMat44::sTranslation(toJoltPosition(center)), ignore);
}

auto PhysicsSystem::overlapBox(const math::Float3& center, const math::Float3& halfExtents, const math::Quat& rotation, world::Entity ignore) const
    -> std::vector<world::Entity>
{
    if (!(halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f))
        return {};
    const float convexRadius = std::min({ JPH::cDefaultConvexRadius, halfExtents.x, halfExtents.y, halfExtents.z });
    JPH::BoxShape box{ toJolt(halfExtents), convexRadius };
    box.SetEmbedded();
    return overlappingEntities(m_impl->physics, box, JPH::RMat44::sRotationTranslation(toJolt(rotation), toJoltPosition(center)), ignore);
}

auto raycast(const world::Registry& registry, const math::Float3& origin, const math::Float3& direction, float maxDistance, world::Entity ignore)
    -> std::optional<RayHit>
{
    const auto* system = registry.context<PhysicsSystem>();
    return system ? system->raycast(origin, direction, maxDistance, ignore) : std::nullopt;
}

auto overlapSphere(const world::Registry& registry, const math::Float3& center, float radius, world::Entity ignore) -> std::vector<world::Entity>
{
    const auto* system = registry.context<PhysicsSystem>();
    return system ? system->overlapSphere(center, radius, ignore) : std::vector<world::Entity>{};
}

auto overlapBox(const world::Registry& registry, const math::Float3& center, const math::Float3& halfExtents, const math::Quat& rotation, world::Entity ignore)
    -> std::vector<world::Entity>
{
    const auto* system = registry.context<PhysicsSystem>();
    return system ? system->overlapBox(center, halfExtents, rotation, ignore) : std::vector<world::Entity>{};
}

void PhysicsSystem::onBodyChanged(entt::registry&, entt::entity entity)
{
    if (m_impl->running)
        m_impl->pending.push_back(entity);
}

void PhysicsSystem::onColliderDestroyed(entt::registry&, entt::entity entity)
{
    m_impl->removeBody(entity);
}
}
