module;

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

export module aegis.world;

import std;
export import aegis.world.registry;
import aegis.math;
export import aegis.world.system; // System and UpdateMode, which begin and update take

export namespace aegis::world
{
/// @brief World contains all entities and systems
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
    }

    /// @brief Queues a change of the entities for the start of the next update
    /// @note For scripts and systems, which run while the world updates: creating, destroying or removing
    ///       there would change what the other systems iterate. The commands run in order, before any system.
    void defer(std::function<void(Registry&)> command) { m_deferred.push_back(std::move(command)); }

    /// @brief Starts the systems that run in 'mode'
    void begin(UpdateMode mode = UpdateMode::Play);

    /// @brief Applies the deferred commands, then runs the systems of 'mode' phase by phase
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
};
}
