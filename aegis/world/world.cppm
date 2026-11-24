module;

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

// aegis.world: the entities, their components and the systems that run them.
//
// Partitions:
//   :entity            Entity and the EnTT names the world exports.
//   :components        The core components: Name, StableId, Transform, the hierarchy, Camera, the lights and post-process.
//   :render_components MeshRenderer and Sky, which name assets.
//   :registry          Registry: the entities' storage, the hierarchy and the StableId lookup.
//   :system            System, Phase and UpdateMode.
//   :time              FixedStep, the accumulator of the fixed steps, and Time, a world's time.
//   :types             The registered components, reachable without naming their type (ComponentStorage).
//   :transform_system  TransformSystem: the global transforms, from the hierarchy.
//   :camera_system     CameraSystem: the cameras' matrices.
export module aegis.world;

import std;
import aegis.core;
import aegis.math;
export import :camera_system;
export import :components;
export import :entity;
export import :registry;
export import :render_components;
export import :system;
export import :time;
export import :transform_system;
export import :types;

export namespace aegis::world
{
/// @brief World contains all entities and systems, and the clock it runs by
class World
{
public:
    World() = default;
    World(const World&) = delete;
    World(World&&) = delete;
    ~World() { reset(); }

    auto operator=(const World&) -> World& = delete;
    auto operator=(World&&) -> World& = delete;

    [[nodiscard]] auto registry() -> Registry& { return m_registry; }
    /// @brief Hands out the delta time of the world's updates, apart from any other world's: a wall clock with a clamp
    ///        by default; assign core::FrameClock::fixed(step) for a deterministic run. Whoever updates the world ticks it.
    /// @note reset() keeps it: the clock belongs to the world, not to the scene in it.
    [[nodiscard]] auto clock() -> core::FrameClock& { return m_clock; }
    /// @brief The world's time as of its last update: frame delta, fixed step, alpha, elapsed time and counters
    /// @note The same as registry().time(), where systems and scripts read it. reset() keeps it.
    [[nodiscard]] auto time() const -> const Time& { return m_registry.time(); }
    /// @brief The length of the world's fixed steps (default 1/60 s) and the most one update runs (default 8)
    /// @note reset() keeps it, with the time gathered towards the next step.
    [[nodiscard]] auto fixedStep() -> FixedStep& { return m_fixedStep; }
    /// @brief The entity with the MainCamera tag, the first ambient light and the first directional light
    /// @note These are queries, not stored entities: they read as null once the entity is destroyed
    ///       (or loses the component), and a scene file needs no special case for them.
    [[nodiscard]] auto mainCamera() const -> Entity { return m_registry.first<MainCamera>(); }
    [[nodiscard]] auto ambientLight() const -> Entity { return m_registry.first<AmbientLight>(); }
    [[nodiscard]] auto directionalLight() const -> Entity { return m_registry.first<DirectionalLight>(); }

    /// @brief Makes the entity the main camera: it gets the MainCamera tag, which moves off any other entity
    void setMainCamera(Entity camera);

    template<SystemDerived T, typename... Args>
        requires std::constructible_from<T, Args...>
    void addSystem(Args&&... args)
    {
        m_systems.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        m_systems.back()->onAttach(m_registry);
    }

    /// @brief Queues a change of the entities for the start of the next update
    /// @note For scripts and systems, which run while the world updates: creating, destroying or removing
    ///       there would change what the other systems iterate. The commands run in order, before any system.
    void defer(std::function<void(Registry&)> command) { m_deferred.push_back(std::move(command)); }

    /// @brief Starts the systems that run in 'mode'
    void begin(UpdateMode mode = UpdateMode::Play);

    /// @brief Applies the deferred commands, advances the time, then runs the systems of 'mode' phase by phase
    /// @note In UpdateMode::Play, Phase::FixedUpdate runs every system's onFixedUpdate once per fixed step the
    ///       frame completes (FixedStep::advance); Edit runs no steps.
    void update(float deltaSeconds, UpdateMode mode = UpdateMode::Play);

    /// @brief Empties the scene: the systems end what they run (scripts get onDestroy) while the
    ///        entities still exist, then every entity and then every system is removed
    /// @note Slots, StableIds and the registry's destroy listeners go with the entities. The scene can be
    ///       filled again afterwards (systems included).
    void reset();

private:
    [[nodiscard]] static auto runs(const System& system, UpdateMode mode) -> bool
    {
        return mode == UpdateMode::Play || system.runsInEdit();
    }

    void applyDeferred();

    Registry m_registry;
    std::vector<std::function<void(Registry&)>> m_deferred;
    std::vector<std::unique_ptr<System>> m_systems;
    core::FrameClock m_clock{ core::FrameClock::clamped() };
    FixedStep m_fixedStep;
};
}
