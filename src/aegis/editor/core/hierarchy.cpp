module aegis.editor.core;
import std;
import aegis.world;

namespace aegis::editor
{
namespace
{
// A node still to be listed, with the depth its row gets.
struct Pending
{
    world::Entity entity;
    std::uint32_t depth;
};

// The roots in the order they were created: the registry's view runs from the newest entity to the oldest.
auto rootsOf(world::Registry& registry) -> std::vector<world::Entity>
{
    std::vector<world::Entity> roots;
    for (const auto handle : registry.view<StableId>())
    {
        const world::Entity entity{ handle };
        if (!registry.has<Parent>(entity) || !registry.get<Parent>(entity).entity)
            roots.push_back(entity);
    }
    std::ranges::reverse(roots);
    return roots;
}
}

auto hierarchyRows(world::Registry& registry, const NodeOpen& isOpen) -> std::vector<HierarchyRow>
{
    std::vector<HierarchyRow> rows;
    std::vector<Pending> stack;
    const auto roots = rootsOf(registry);
    for (const auto root : roots | std::views::reverse) // The first root is popped first
        stack.push_back({ root, 0 });

    while (!stack.empty())
    {
        const auto [entity, depth] = stack.back();
        stack.pop_back();

        const auto id = registry.get<StableId>(entity);
        const bool hasChildren = registry.has<Children>(entity) && registry.get<Children>(entity).first;
        const bool open = hasChildren && isOpen(id);
        rows.push_back({ .id = id, .entity = entity, .depth = depth, .hasChildren = hasChildren, .open = open });
        if (!open)
            continue;

        // Pushed from the last child back, so the first child is popped first
        for (auto child = registry.get<Children>(entity).last; child; child = registry.get<Siblings>(child).prev)
            stack.push_back({ child, depth + 1 });
    }
    return rows;
}
}
