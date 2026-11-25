export module aegis.physics:physics_system;

import std;
import aegis.math;
import aegis.world;
import :components;
import :queries;

export namespace aegis::physics
{
/// @brief Simulates a world's rigid bodies and characters with Jolt Physics: one Jolt physics system per world, stepped
///        once per fixed step while the world plays (it doesn't run in UpdateMode::Edit, so editing leaves the bodies
///        alone).
///
/// - onBegin gives each entity with a Collider a body at its global pose (location and rotation; the global scale
///   scales the shape), and dynamic and kinematic ones a DynamicTag; each entity with a CharacterController becomes a
///   character at its global location, with a DynamicTag. A Collider, RigidBody or CharacterController added or
///   removed later changes the bodies at the next fixed step; a removed Collider, CharacterController or entity loses
///   its body or character at once.
/// - Each fixed step moves the kinematic bodies to their entity's pose and the characters by their desired velocity
///   (CharacterController), steps Jolt by the step and keeps each dynamic body's pose and each character's location
///   before and after it.
/// - After each step it emits the contact events (ContactBegin, ContactEnd) of the pairs that started or stopped
///   touching, in pair order; a body that goes away (or is replaced, when its Collider or RigidBody changes) ends its
///   contacts at once.
/// - Once per frame, after the fixed steps (Phase::FixedUpdate) and so before the TransformSystem, it writes each
///   dynamic body's pose and each character's location, blended by the world's alpha, into its entity's Transform: in
///   its parent's space when it has one, keeping the Transform's scale (and a character's rotation). The blend runs from the step before the last (alpha 0) to the last one, so
///   the Transform trails the simulation by one step.
/// - An entity's pose (at begin, for a kinematic body, and its parent's for the write) is composed from the Transforms
///   of it and its ancestors, as the TransformSystem computes its GlobalTransform: the GlobalTransforms themselves are
///   not computed yet at begin and lag a frame behind a script that moved them.
/// @note It makes itself the registry's context (world::Registry::setContext), where the engine's other code finds it.
class PhysicsSystem : public world::System
{
public:
    /// @brief The engine's down axis times 9.81 m/s²
    static constexpr math::Float3 DEFAULT_GRAVITY{ math::world::up<float> * -9.81f };

    PhysicsSystem();
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem(PhysicsSystem&&) = delete;
    ~PhysicsSystem() override;

    auto operator=(const PhysicsSystem&) -> PhysicsSystem& = delete;
    auto operator=(PhysicsSystem&&) -> PhysicsSystem& = delete;

    /// @brief Right after the fixed steps: the interpolated poses reach the Transforms before scripts and the
    ///        TransformSystem read them
    [[nodiscard]] auto phase() const -> world::Phase override { return world::Phase::FixedUpdate; }

    void onAttach(world::Registry& registry) override;
    void onDetach(world::Registry& registry) override;
    void onBegin(world::Registry& registry) override;
    void onFixedUpdate(world::Registry& registry, float stepSeconds) override;
    void onUpdate(world::Registry& registry, float deltaSeconds) override;
    void onReset(world::Registry& registry) override;

    [[nodiscard]] auto gravity() const -> math::Float3;
    /// @brief The world's gravity in m/s², DEFAULT_GRAVITY unless changed; takes effect at the next step
    void setGravity(const math::Float3& gravity);

    /// @brief The number of bodies in the simulation, static ones included
    [[nodiscard]] auto bodyCount() const -> std::size_t;
    /// @brief Whether the entity has a body (a character's inner body doesn't count)
    [[nodiscard]] auto hasBody(world::Entity entity) const -> bool;
    /// @brief Whether the entity is a character (CharacterController)
    [[nodiscard]] auto hasCharacter(world::Entity entity) const -> bool;

    /// @brief physics::raycast on this world's bodies
    [[nodiscard]] auto raycast(const math::Float3& origin, const math::Float3& direction, float maxDistance, world::Entity ignore) const -> std::optional<RayHit>;
    /// @brief physics::overlapSphere on this world's bodies
    [[nodiscard]] auto overlapSphere(const math::Float3& center, float radius, world::Entity ignore) const -> std::vector<world::Entity>;
    /// @brief physics::overlapBox on this world's bodies
    [[nodiscard]] auto overlapBox(const math::Float3& center, const math::Float3& halfExtents, const math::Quat& rotation, world::Entity ignore) const
        -> std::vector<world::Entity>;

private:
    struct Impl;

    void onBodyChanged(entt::registry& registry, entt::entity entity);
    void onColliderDestroyed(entt::registry& registry, entt::entity entity);
    void onCharacterDestroyed(entt::registry& registry, entt::entity entity);

    std::unique_ptr<Impl> m_impl;
};
}
