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
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
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

using EntityKey = std::underlying_type_t<entt::entity>;

auto keyOf(entt::entity entity) -> EntityKey
{
    return std::to_underlying(entity);
}

auto entityOf(EntityKey key) -> world::Entity
{
    return world::Entity{ static_cast<entt::entity>(key) };
}

// Two entities whose bodies touch, the lower ID first.
struct ContactPair
{
    EntityKey a;
    EntityKey b;

    auto operator<=>(const ContactPair&) const = default;

    [[nodiscard]] auto contains(EntityKey entity) const -> bool { return a == entity || b == entity; }
};

// A pair of entities found touching in a step, with where.
struct ContactReport
{
    ContactPair pair;
    math::Float3 point;
    math::Float3 normal; // From a toward b
};

// The report of two entities touching at 'point', with 'normal' pointing from 'first' toward 'second'; none for an
// entity touching itself.
auto makeReport(EntityKey first, EntityKey second, const math::Float3& point, const math::Float3& normal) -> std::optional<ContactReport>
{
    if (first == second)
        return std::nullopt;
    if (first < second)
        return ContactReport{ .pair = { first, second }, .point = point, .normal = normal };
    return ContactReport{ .pair = { second, first }, .point = point, .normal = -normal };
}

// Collects the body pairs that touch during a step. Jolt calls it from its job threads, so it only records, under a
// lock; the system sorts the records and emits the events on the main thread after the step.
class ContactCollector final : public JPH::ContactListener
{
public:
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        record(body1, body2, manifold);
    }

    void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        record(body1, body2, manifold);
    }

    // A report from outside Jolt's step: a character's contact.
    void add(const ContactReport& report)
    {
        const std::scoped_lock lock{ m_mutex };
        m_reports.push_back(report);
    }

    // The reports since the last call, in the order they came.
    auto take() -> std::vector<ContactReport>
    {
        const std::scoped_lock lock{ m_mutex };
        return std::exchange(m_reports, {});
    }

private:
    void record(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
    {
        // The middle of the contact points on both bodies
        auto point = manifold.mBaseOffset;
        if (const auto count = manifold.mRelativeContactPointsOn1.size(); count != 0)
        {
            JPH::Vec3 sum = JPH::Vec3::sZero();
            for (JPH::uint i = 0; i < count; ++i)
                sum += manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i];
            point += sum / (2.0f * static_cast<float>(count));
        }
        auto report = makeReport(static_cast<EntityKey>(body1.GetUserData()), static_cast<EntityKey>(body2.GetUserData()), fromJolt(point),
            fromJolt(JPH::RVec3{ manifold.mWorldSpaceNormal }));
        if (!report)
            return;
        const std::scoped_lock lock{ m_mutex };
        m_reports.push_back(*report);
    }

    std::mutex m_mutex;
    std::vector<ContactReport> m_reports;
};

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

// A character's capsule standing on its origin (the feet) along math::world::up, or why it can't be made.
auto characterShape(const CharacterController& controller) -> std::expected<JPH::ShapeRefC, std::string>
{
    const float halfCylinder = 0.5f * controller.height - controller.radius;
    if (!(controller.radius > 0.0f && halfCylinder > 0.0f))
        return std::unexpected{
            std::format("a character needs a radius above zero and a height above twice its radius, not {} and {}", controller.radius, controller.height)
        };
    // The cylinder of Jolt's capsule runs along its Y axis; ours stands along math::world::up (Z).
    const JPH::Ref<JPH::CapsuleShapeSettings> capsule = new JPH::CapsuleShapeSettings{ halfCylinder, controller.radius };
    const JPH::Ref<JPH::RotatedTranslatedShapeSettings> placed = new JPH::RotatedTranslatedShapeSettings{
        toJolt(math::world::up<float> * (0.5f * controller.height)), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI), capsule
    };
    const auto result = placed->Create();
    if (result.HasError())
        return std::unexpected{ std::string{ result.GetError().c_str() } };
    return result.Get();
}

auto nameOf(world::Registry& registry, world::Entity entity) -> std::string
{
    return registry.has<Name>(entity) ? registry.get<Name>(entity).name : std::string{};
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

// Writes a global pose into the entity's Transform, in its parent's space when it has one. Keeps the Transform's scale,
// and its rotation when 'rotation' is none.
void writePose(world::Registry& registry, world::Entity entity, const math::Float3& location, std::optional<math::Quat> rotation)
{
    auto& transform = registry.get<Transform>(entity);
    const auto parent = parentOf(registry, entity);
    if (!parent)
    {
        transform.location = location;
        if (rotation)
            transform.rotation = *rotation;
        return;
    }
    const auto parentGlobal = globalOf(registry, parent);
    const auto globalRotation = rotation ? *rotation : math::decomposeTRS(world::detail::compose(parentGlobal, transform).matrix).rotation;
    const auto local = world::detail::toLocal(parentGlobal, math::transformMatrix(location, globalRotation, parentGlobal.scale() * transform.scale));
    transform.location = local.location;
    if (rotation)
        transform.rotation = local.rotation;
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

    struct Character
    {
        JPH::Ref<JPH::CharacterVirtual> character;
        // Its feet before and after the last step, for the frame's blend
        math::Float3 previousLocation;
        math::Float3 location;
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
    ContactCollector contacts; // Before the physics, which calls it
    JPH::PhysicsSystem physics;

    std::unordered_map<entt::entity, Body> bodies;
    std::unordered_map<entt::entity, Character> characters;
    std::vector<entt::entity> pending;    // Entities whose Collider or RigidBody came or went since the last step
    std::vector<ContactPair> touching;    // Sorted: the pairs that sent a ContactBegin and no ContactEnd yet
    world::Registry* registry{ nullptr }; // The registry of the world it is attached to: where the contact events go
    bool running{ false };                // Between onBegin and onReset: bodies exist and follow the signals

    Impl()
    {
        physics.Init(MAX_BODIES, 0, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, broadPhase, objectVsBroadPhase, objectPairs);
        physics.SetGravity(toJolt(DEFAULT_GRAVITY));
        physics.SetContactListener(&contacts);
        // Resting bodies sink into each other by up to the penetration slop (Jolt: 2 cm), which shows; 2 mm still rests
        // without jitter.
        auto settings = physics.GetPhysicsSettings();
        settings.mPenetrationSlop = PENETRATION_SLOP;
        physics.SetPhysicsSettings(settings);
    }

    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;

    ~Impl() { removeAll(); }

    // Removes the entity's body (not its character).
    void removeBody(entt::entity entity)
    {
        const auto found = bodies.find(entity);
        if (found == bodies.end())
            return;
        auto& bodyInterface = physics.GetBodyInterface();
        bodyInterface.RemoveBody(found->second.id);
        bodyInterface.DestroyBody(found->second.id);
        bodies.erase(found);
        endContacts(keyOf(entity));
    }

    // Removes the entity's character, and with it its inner body.
    void removeCharacter(entt::entity entity)
    {
        if (characters.erase(entity) != 0)
            endContacts(keyOf(entity));
    }

    // Ends the contacts of an entity that lost its body.
    void endContacts(EntityKey entity)
    {
        const auto [first, last] = std::ranges::stable_partition(touching, [entity](const ContactPair& pair) { return !pair.contains(entity); });
        if (registry)
        {
            for (auto pair = first; pair != last; ++pair)
                registry->events().emit(ContactEnd{ .a = entityOf(pair->a), .b = entityOf(pair->b) });
        }
        touching.erase(first, last);
    }

    // Whether the entity's body is awake: an awake body reports its contacts every step, a sleeping one keeps them. A
    // character reports its contacts every step.
    [[nodiscard]] auto isActive(EntityKey entity) const -> bool
    {
        if (characters.contains(static_cast<entt::entity>(entity)))
            return true;
        const auto found = bodies.find(static_cast<entt::entity>(entity));
        return found != bodies.end() && physics.GetBodyInterface().IsActive(found->second.id);
    }

    // Emits the contact events of the step that just ran: a ContactEnd for each pair that stopped touching, then a
    // ContactBegin for each that started, both in pair order.
    void emitContacts(world::Registry& registry)
    {
        auto reports = contacts.take();
        std::ranges::stable_sort(reports, {}, &ContactReport::pair);
        const auto [first, last] = std::ranges::unique(reports, {}, &ContactReport::pair);
        reports.erase(first, last);
        const auto reported = [&reports](const ContactPair& pair) { return std::ranges::binary_search(reports, pair, {}, &ContactReport::pair); };

        // Touching now: the pairs reported in this step, and the earlier ones whose bodies all sleep (Jolt reports
        // nothing for them, but they still touch).
        std::vector<ContactPair> now;
        now.reserve(reports.size() + touching.size());
        for (const auto& report : reports)
            now.push_back(report.pair);
        for (const auto& pair : touching)
        {
            if (!reported(pair) && !isActive(pair.a) && !isActive(pair.b))
                now.push_back(pair);
        }
        std::ranges::sort(now);

        auto& events = registry.events();
        std::vector<ContactPair> ended;
        std::ranges::set_difference(touching, now, std::back_inserter(ended));
        for (const auto& pair : ended)
            events.emit(ContactEnd{ .a = entityOf(pair.a), .b = entityOf(pair.b) });
        for (const auto& report : reports)
        {
            if (!std::ranges::binary_search(touching, report.pair))
                events.emit(ContactBegin{ .a = entityOf(report.pair.a), .b = entityOf(report.pair.b), .point = report.point, .normal = report.normal });
        }
        touching = std::move(now);
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
        characters.clear();
        pending.clear();
        touching.clear();
        contacts.take();
    }

    // Gives the entity a character for its CharacterController, or else a body for its Collider, replacing the one it
    // had; none without either.
    void createBody(world::Registry& registry, world::Entity entity)
    {
        // A Collider or RigidBody that came or went on a character changes nothing: it stays as it moves.
        if (characters.contains(entity.id()) && registry.has<CharacterController>(entity))
        {
            if (registry.has<Collider>(entity) || registry.has<RigidBody>(entity))
                log::warn("The entity '{}' has a CharacterController and a Collider or RigidBody: it is a character, they do nothing", nameOf(registry, entity));
            return;
        }
        removeBody(entity.id());
        removeCharacter(entity.id());
        if (registry.has<CharacterController>(entity))
        {
            createCharacter(registry, entity);
            return;
        }
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

    void createCharacter(world::Registry& registry, world::Entity entity)
    {
        if (registry.has<Collider>(entity) || registry.has<RigidBody>(entity))
            log::warn("The entity '{}' has a CharacterController and a Collider or RigidBody: it is a character, they do nothing", nameOf(registry, entity));

        const auto& controller = registry.get<CharacterController>(entity);
        auto shape = characterShape(controller);
        if (!shape)
        {
            log::warn("The entity '{}' is no character: {}", nameOf(registry, entity), shape.error());
            return;
        }

        const auto up = toJolt(math::world::up<float>);
        const JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings{};
        settings->mShape = *shape;
        settings->mUp = up;
        settings->mSupportingVolume = JPH::Plane{ up, -controller.radius }; // Contacts in the bottom half-sphere support it
        settings->mMaxSlopeAngle = math::radians(controller.maxSlope);
        settings->mMaxStrength = controller.pushStrength;
        settings->mInnerBodyShape = *shape;
        settings->mInnerBodyLayer = layers::MOVING;

        const auto feet = math::decomposeTRS(globalOf(registry, entity).matrix).location;
        Character character{
            .character = new JPH::CharacterVirtual{ settings, toJoltPosition(feet), JPH::Quat::sIdentity(), toUserData(entity.id()), &physics },
            .previousLocation = feet,
            .location = feet,
        };
        characters.emplace(entity.id(), std::move(character));
        if (!registry.has<DynamicTag>(entity))
            registry.add<DynamicTag>(entity);
    }

    // Moves each character by its desired velocity and gravity for a step, and reports what it touches.
    void moveCharacters(world::Registry& registry, float stepSeconds)
    {
        const auto up = toJolt(math::world::up<float>);
        const auto gravity = physics.GetGravity();
        const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter{ objectVsBroadPhase, layers::MOVING };
        const JPH::DefaultObjectLayerFilter layerFilter{ objectPairs, layers::MOVING };
        const JPH::BodyFilter bodyFilter;
        const JPH::ShapeFilter shapeFilter;
        auto& bodyInterface = physics.GetBodyInterface();

        for (auto& [entity, character] : characters)
        {
            auto& controller = registry.get<CharacterController>(world::Entity{ entity });
            auto& virtualCharacter = *character.character;
            virtualCharacter.SetMaxSlopeAngle(math::radians(controller.maxSlope));
            virtualCharacter.SetMaxStrength(controller.pushStrength);

            // Jolt's recipe: on the ground it moves with the ground, plus a jump; in the air it keeps its vertical
            // speed. Gravity and the walk come on top.
            const auto desired = toJolt(controller.desiredVelocity);
            const float jump = desired.Dot(up);
            const auto walk = desired - jump * up;
            virtualCharacter.UpdateGroundVelocity();
            const auto groundVelocity = virtualCharacter.GetGroundVelocity();
            const auto verticalVelocity = virtualCharacter.GetLinearVelocity().Dot(up) * up;
            const bool towardGround = (verticalVelocity - groundVelocity).Dot(up) < 0.1f;
            auto velocity = virtualCharacter.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && towardGround
                                ? groundVelocity + jump * up
                                : verticalVelocity;
            velocity += walk + gravity * stepSeconds;
            virtualCharacter.SetLinearVelocity(velocity);

            JPH::CharacterVirtual::ExtendedUpdateSettings update;
            update.mStickToFloorStepDown = -controller.stepHeight * up;
            update.mWalkStairsStepUp = controller.stepHeight * up;
            virtualCharacter.ExtendedUpdate(stepSeconds, gravity, update, broadPhaseFilter, layerFilter, bodyFilter, shapeFilter, temp);

            controller.grounded = virtualCharacter.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
            character.previousLocation = character.location;
            character.location = fromJolt(virtualCharacter.GetPosition());

            for (const auto& contact : virtualCharacter.GetActiveContacts())
            {
                if (!contact.mHadCollision || contact.mWasDiscarded || contact.mCharacterB != nullptr)
                    continue;
                // The contact's normal points toward the character
                const auto other = static_cast<EntityKey>(bodyInterface.GetUserData(contact.mBodyB));
                if (auto report = makeReport(keyOf(entity), other, fromJolt(contact.mPosition), -fromJolt(JPH::RVec3{ contact.mContactNormal })))
                    contacts.add(*report);
            }
        }
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
    m_impl->registry = &registry;
    registry.onConstruct<Collider>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onConstruct<RigidBody>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onDestroy<RigidBody>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onDestroy<Collider>().connect<&PhysicsSystem::onColliderDestroyed>(this);
    registry.onConstruct<CharacterController>().connect<&PhysicsSystem::onBodyChanged>(this);
    registry.onDestroy<CharacterController>().connect<&PhysicsSystem::onCharacterDestroyed>(this);
}

void PhysicsSystem::onDetach(world::Registry& registry)
{
    m_impl->removeAll();
    m_impl->running = false;
    m_impl->registry = nullptr;
    registry.onConstruct<Collider>().disconnect(this);
    registry.onConstruct<RigidBody>().disconnect(this);
    registry.onDestroy<RigidBody>().disconnect(this);
    registry.onDestroy<Collider>().disconnect(this);
    registry.onConstruct<CharacterController>().disconnect(this);
    registry.onDestroy<CharacterController>().disconnect(this);
    if (registry.context<PhysicsSystem>() == this)
        registry.eraseContext<PhysicsSystem>();
}

void PhysicsSystem::onBegin(world::Registry& registry)
{
    m_impl->removeAll();

    std::vector<entt::entity> entities;
    for (const auto entity : registry.view<Collider>())
        entities.push_back(entity);
    for (const auto entity : registry.view<CharacterController>())
        entities.push_back(entity);
    std::ranges::sort(entities);
    const auto [first, last] = std::ranges::unique(entities);
    entities.erase(first, last);
    for (const auto entity : entities)
        m_impl->createBody(registry, world::Entity{ entity });

    std::size_t bodiesWithoutCollider{ 0 };
    for ([[maybe_unused]] const auto entity : registry.view<RigidBody>(entt::exclude<Collider, CharacterController>))
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

    // Before the step, as Jolt advises: the bodies a character pushed move in it.
    m_impl->moveCharacters(registry, stepSeconds);

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

    m_impl->emitContacts(registry);
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
        writePose(registry, world::Entity{ id }, math::lerp(body.previousLocation, body.location, alpha), math::slerp(body.previousRotation, body.rotation, alpha));
    }
    for (const auto& [id, character] : m_impl->characters)
        writePose(registry, world::Entity{ id }, math::lerp(character.previousLocation, character.location, alpha), std::nullopt);
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

auto PhysicsSystem::hasCharacter(world::Entity entity) const -> bool
{
    return m_impl->characters.contains(entity.id());
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

void PhysicsSystem::onCharacterDestroyed(entt::registry&, entt::entity entity)
{
    m_impl->removeCharacter(entity);
    if (m_impl->running)
        m_impl->pending.push_back(entity); // Its Collider, if any, makes a body now
}
}
