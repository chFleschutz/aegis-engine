module aegis.editor.core;
import std;
import aegis.core;
import aegis.serialize;
import aegis.world;
import aegis.world.render_components;

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

// The entities whose name holds 'filter' and their ancestors: what a filtered hierarchy lists.
auto matchesAndAncestors(world::Registry& registry, std::string_view filter) -> std::unordered_set<std::uint64_t>
{
    std::unordered_set<std::uint64_t> kept;
    for (const auto handle : registry.view<StableId>())
    {
        const world::Entity entity{ handle };
        const std::string_view name = registry.has<Name>(entity) ? std::string_view{ registry.get<Name>(entity).name } : std::string_view{};
        if (!core::containsIgnoringCase(name, filter))
            continue;
        // Up to the first ancestor that is kept already: its own ancestors are
        for (auto node = entity; node && kept.insert(registry.get<StableId>(node).value).second;)
            node = registry.has<Parent>(node) ? registry.get<Parent>(node).entity : world::Entity{};
    }
    return kept;
}
}

auto entityKind(world::Registry& registry, world::Entity entity) -> EntityKind
{
    if (registry.has<Camera>(entity))
        return EntityKind::Camera;
    if (registry.has<DirectionalLight>(entity))
        return EntityKind::DirectionalLight;
    if (registry.has<PointLight>(entity))
        return EntityKind::PointLight;
    if (registry.has<MeshRenderer>(entity))
        return EntityKind::Mesh;
    if (registry.has<serialize::SceneInstance>(entity))
        return EntityKind::SceneInstance;
    return EntityKind::Empty;
}

auto hierarchyRows(world::Registry& registry, const NodeOpen& isOpen, std::string_view filter) -> std::vector<HierarchyRow>
{
    const bool filtered = !filter.empty();
    const auto kept = filtered ? matchesAndAncestors(registry, filter) : std::unordered_set<std::uint64_t>{};
    const auto listed = [&](world::Entity entity) { return !filtered || kept.contains(registry.get<StableId>(entity).value); };

    std::vector<HierarchyRow> rows;
    std::vector<Pending> stack;
    const auto roots = rootsOf(registry);
    for (const auto root : roots | std::views::reverse) // The first root is popped first
    {
        if (listed(root))
            stack.push_back({ root, 0 });
    }

    while (!stack.empty())
    {
        const auto [entity, depth] = stack.back();
        stack.pop_back();

        const auto id = registry.get<StableId>(entity);
        bool hasChildren = false;
        if (registry.has<Children>(entity))
        {
            for (auto child = registry.get<Children>(entity).first; child && !hasChildren; child = registry.get<Siblings>(child).next)
                hasChildren = listed(child);
        }
        const bool open = hasChildren && (filtered || isOpen(id));
        rows.push_back({ .id = id, .entity = entity, .depth = depth, .hasChildren = hasChildren, .open = open });
        if (!open)
            continue;

        // Pushed from the last child back, so the first child is popped first
        for (auto child = registry.get<Children>(entity).last; child; child = registry.get<Siblings>(child).prev)
        {
            if (listed(child))
                stack.push_back({ child, depth + 1 });
        }
    }
    return rows;
}
}
