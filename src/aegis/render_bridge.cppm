export module aegis.render_bridge;

import std;
import aegis.render_graph;
import aegis.world;
import aegis.world.registry;

// Between the world and the render graph, which knows no world: the instance store follows the world's mesh
// renderers, and each frame's FrameScene is copied out of the world.

export namespace aegis
{
/// @brief Keeps a render_graph::InstanceStore in step with the aegis::MeshRenderer components of a world.
///        Renderers that come and go, and entities that move between the static and dynamic sets (DynamicTag),
///        are followed through the registry's signals; what an instance draws is read by sync().
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
    auto disconnect() -> void;

    /// @brief Brings the store up to date with the world, after its update: every dynamic instance, and the static
    ///        ones if the static version changed. A renderer is drawn while it has a mesh, a material and a
    ///        GlobalTransform.
    auto sync() -> void;

private:
    struct Tracked
    {
        std::optional<render_graph::InstanceId> instance; // None while it cannot be drawn
        bool dynamic;
        bool warned; // Its instance could not be added; logged once
    };

    auto onRendererConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onRendererDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicConstruct(entt::registry& registry, entt::entity entity) -> void;
    auto onDynamicDestroy(entt::registry& registry, entt::entity entity) -> void;
    auto setDynamic(entt::entity entity, bool dynamic) -> void;
    /// @brief Adds, updates or removes the instance of 'entity' from its components.
    auto refresh(entt::entity entity, Tracked& tracked) -> void;

    render_graph::InstanceStore& m_store;
    world::Registry* m_registry{ nullptr };
    std::unordered_map<entt::entity, Tracked> m_tracked;
    std::optional<std::uint64_t> m_syncedVersion; // The static version sync() last read the static entities at
};

/// @brief The FrameScene of a world whose transforms and camera matrices are current (after its update):
///        the main camera if it has a Camera and a GlobalTransform, the ambient and directional light,
///        every point light, the environment of the first aegis::Sky, and 'instances' (kept by an InstanceBridge).
///        Reads the world, never writes it.
[[nodiscard]] auto extractFrameScene(world::World& world, const render_graph::InstanceStore& instances)
    -> render_graph::FrameScene;
}
