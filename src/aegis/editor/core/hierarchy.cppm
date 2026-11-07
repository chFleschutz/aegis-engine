export module aegis.editor.core:hierarchy;
import std;
import aegis.world;
import :selection;

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

/// @brief 'rows' with the nodes 'isOpen' calls closed closed: their rows' open turns false and their subtrees are left
///        out. 'isOpen' is asked once per open row that is not left out. For a filtered hierarchy, whose rows are all open
///        (hierarchyRows), with a tree state of its own.
[[nodiscard]] auto closeRows(std::span<const HierarchyRow> rows, const NodeOpen& isOpen) -> std::vector<HierarchyRow>;

/// @brief The index of the row of 'id' in 'rows', or none: where a list scrolls to show it.
[[nodiscard]] auto rowIndex(std::span<const HierarchyRow> rows, StableId id) -> std::optional<std::size_t>;

/// @brief The ancestors of 'entity', its parent first: the nodes to open for its row to be listed.
[[nodiscard]] auto ancestorIds(world::Registry& registry, world::Entity entity) -> std::vector<StableId>;

/// @brief The rows from the one of 'from' to the one of 'to', both included, in that order (a Shift+click from the
///        active row): the last is 'to'. Only 'to' when 'from' is not among the rows (under a closed node, filtered
///        out); empty when 'to' is not among them.
[[nodiscard]] auto rangeSelection(std::span<const HierarchyRow> rows, StableId from, StableId to) -> std::vector<StableId>;

/// @brief The selected entities that have no selected ancestor, top to bottom as the hierarchy lists them: what Delete,
///        Duplicate and a drag act on (a selected entity's descendants go with it). Ids of no entity are skipped.
[[nodiscard]] auto selectedRoots(world::Registry& registry, const Selection& selection) -> std::vector<world::Entity>;
}
