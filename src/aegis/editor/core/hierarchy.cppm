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

/// @brief Whether the hierarchy node of an entity is open, asked once per node that has children.
using NodeOpen = std::function<bool(StableId)>;

/// @brief The rows the hierarchy shows, top to bottom: the roots, then each node's children after it, both in
///        sibling order (the order of the list and of the scene file). The children of a closed node are left out.
/// @note Walks iteratively, so a deep tree cannot overflow the stack. The rows are a flat pre-order list, so a
///       filter can drop rows and keep the ancestors of those it keeps.
[[nodiscard]] auto hierarchyRows(world::Registry& registry, const NodeOpen& isOpen) -> std::vector<HierarchyRow>;
}
