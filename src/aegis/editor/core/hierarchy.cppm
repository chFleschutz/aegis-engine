export module aegis.editor.core:hierarchy;
import std;
import aegis.world;

export namespace aegis::editor
{
/// @brief One row of the hierarchy panel: an entity, how deep it sits, and whether its children follow.
struct HierarchyRow
{
    StableId id;
    world::Entity entity;
    std::uint32_t depth{ 0 }; ///< 0 for a root
    bool hasChildren{ false };
    bool open{ false }; ///< Whether its children follow it in the rows (never for a node without children)
};

/// @brief What an entity is, for the icon of its row.
enum class EntityKind
{
    Camera,
    DirectionalLight,
    PointLight,
    Mesh,
    SceneInstance, ///< The holder of a scene instance (serialize::SceneInstance)
    Empty,
};

/// @brief The kind of 'entity': the first of Camera, DirectionalLight, PointLight, Mesh (a MeshRenderer) and
///        SceneInstance it has a component for, else Empty.
[[nodiscard]] auto entityKind(world::Registry& registry, world::Entity entity) -> EntityKind;

/// @brief Whether the hierarchy node of an entity is open, asked once per node that has children.
using NodeOpen = std::function<bool(StableId)>;

/// @brief The rows the hierarchy shows, top to bottom: the roots, then each node's children after it, both in
///        sibling order (the order of the list and of the scene file). The children of a closed node are left out.
///
///        With a non-empty 'filter' only the entities whose name holds it (ignoring case) and their ancestors are
///        listed, the ancestors open whatever 'isOpen' says (it is not asked), and a node shows only the children
///        that are listed: hasChildren and open are about those. The children of a match are not listed for
///        being its children.
/// @note Walks iteratively, so a deep tree cannot overflow the stack.
[[nodiscard]] auto hierarchyRows(world::Registry& registry, const NodeOpen& isOpen, std::string_view filter = {}) -> std::vector<HierarchyRow>;
}
