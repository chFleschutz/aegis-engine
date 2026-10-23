export module aegis.render_bridge;

import std;
import aegis.render_graph;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;

// Between the world and the render graph, which knows no world: the instance store follows the world's mesh
// renderers, and each frame's FrameScene is copied out of the world.

export namespace aegis
{
/// @brief The instance an InstanceBridge keeps for an entity's MeshRenderer: added and removed with the renderer,
///        on every entity of a connected world. Runtime only: scene files do not hold it.
struct RenderInstance
{
    std::optional<render_graph::InstanceId> id; // None while the renderer cannot be drawn
    bool warned{ false };                       // Its instance could not be stored; logged once
};

/// @brief Keeps a render_graph::InstanceStore in step with the aegis::MeshRenderer components of a world.
///        Renderers that come and go, and entities that move between the static and dynamic sets (DynamicTag),
///        are followed through the registry's signals; what an instance draws is read by sync(). Each renderer's
///        entity holds the id of its instance in a RenderInstance.
///
///        Static entities follow the static/dynamic rule (aegis::DynamicTag): sync() reads them again only when
///        the registry's static version changed. It also reports every change to the static set to that version,
///        and marks new static entities dirty so the TransformSystem computes their global transforms.
class InstanceBridge
{
public:
    explicit InstanceBridge(render_graph::InstanceStore& store);
    InstanceBridge(const InstanceBridge&) = delete;
    InstanceBridge(InstanceBridge&&) = delete;
    ~InstanceBridge();

    auto operator=(const InstanceBridge&) -> InstanceBridge& = delete;
    auto operator=(InstanceBridge&&) -> InstanceBridge& = delete;

    /// @brief Follows the registry's mesh renderers, starting with the existing ones, which the next sync() adds.
    ///        Empties the store first, so a second connect never adds an instance twice.
    auto connect(world::Registry& registry) -> void;
    /// @brief Stops following the registry, removes its RenderInstance components and empties the store.
    auto disconnect() -> void;

    /// @brief Brings the store up to date with the world, after its update: every dynamic instance, and the static
    ///        ones if the static version changed. A renderer is drawn while it has a mesh and a material.
    auto sync() -> void;

private:
    auto onRendererConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onRendererDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto onInstanceDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto setDynamic(entt::registry& registry, entt::entity entity, bool dynamic) -> void;
    /// @brief Adds, updates or removes the instance of 'entity' from its components.
    auto refresh(entt::entity entity, RenderInstance& instance, const MeshRenderer& renderer,
        const GlobalTransform& transform, bool dynamic) -> void;

    render_graph::InstanceStore& m_store;
    world::Registry* m_registry{ nullptr };
    std::optional<std::uint64_t> m_syncedVersion; // The static version sync() last read the static entities at
};

/// @brief The FrameScene of a world whose transforms and camera matrices are current (after its update):
///        the main camera if it has a Camera and a GlobalTransform, the ambient and directional light,
///        every point light, the environment of the first aegis::Sky, and 'instances' (kept by an InstanceBridge).
///        Reads the world, never writes it.
[[nodiscard]] auto extractFrameScene(world::World& world, const render_graph::InstanceStore& instances)
    -> render_graph::FrameScene;

/// @brief The worlds an app holds, each with the instances drawn of it: an InstanceStore of its own, which an
///        InstanceBridge keeps in step with the world. A world is set up (its systems added) when it is created and
///        when it is reset, and it begins (World::begin) with its first update after that, in that update's mode.
///        A world is named by reference, valid until it is destroyed.
class Worlds
{
public:
    /// @brief Adds the systems of a new or reset world.
    using Setup = std::function<void(world::World& world)>;

    explicit Worlds(Setup setup);
    Worlds(const Worlds&) = delete;
    Worlds(Worlds&&) = delete;
    ~Worlds();

    auto operator=(const Worlds&) -> Worlds& = delete;
    auto operator=(Worlds&&) -> Worlds& = delete;

    /// @brief A new, empty world, set up, with instances of its own.
    [[nodiscard]] auto create() -> world::World&;
    /// @brief Destroys 'world', one of these, with its instances. Its systems end what they run while its entities
    ///        still exist (World::reset).
    auto destroy(world::World& world) -> void;
    /// @brief Empties 'world', one of these (World::reset), and sets it up again, ready for a scene's entities. It
    ///        begins again with its next update.
    auto reset(world::World& world) -> void;
    /// @brief Updates 'world', one of these, by 'deltaSeconds' in 'mode' (World::update), beginning it in 'mode' first
    ///        if this is its first update since it was created or reset.
    auto update(world::World& world, float deltaSeconds, world::UpdateMode mode) -> void;
    /// @brief Brings the instances of 'world', one of these, up to date with it (InstanceBridge::sync), after its update.
    auto sync(world::World& world) -> void;

    [[nodiscard]] auto contains(const world::World& world) const -> bool;
    /// @brief The worlds, in the order they were created.
    [[nodiscard]] auto all() -> std::vector<world::World*>;
    /// @brief The instances of 'world', one of these, as its last sync() left them.
    [[nodiscard]] auto instances(const world::World& world) -> render_graph::InstanceStore&;

private:
    struct Entry
    {
        world::World world;
        render_graph::InstanceStore instances;
        InstanceBridge bridge{ instances }; // Goes first, disconnecting from the world while it exists
        bool begun{ false };                // Since the world was created or last reset
    };

    [[nodiscard]] auto entry(const world::World& world) -> Entry&;

    Setup m_setup;
    std::vector<std::unique_ptr<Entry>> m_entries;
};
}
