module aegis.editor.core;
import std;
import aegis.core;
import aegis.scripting.script;
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
    if (registry.has<AmbientLight>(entity))
        return EntityKind::AmbientLight;
    if (registry.has<Sky>(entity))
        return EntityKind::Sky;
    if (registry.has<MeshRenderer>(entity))
        return EntityKind::Mesh;
    if (registry.has<serialize::SceneInstance>(entity))
        return EntityKind::SceneInstance;
    if (registry.has<scripting::Scripts>(entity) && !registry.get<scripting::Scripts>(entity).entries.empty())
        return EntityKind::Scripted;
    if (registry.has<Children>(entity) && registry.get<Children>(entity).count > 0)
        return EntityKind::Group;
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

auto closeRows(std::span<const HierarchyRow> rows, const NodeOpen& isOpen) -> std::vector<HierarchyRow>
{
    std::vector<HierarchyRow> result;
    result.reserve(rows.size());
    std::optional<std::uint32_t> closedDepth; // Of the closed node whose subtree is being skipped
    for (auto row : rows)
    {
        if (closedDepth && row.depth > *closedDepth)
            continue;
        closedDepth.reset();
        if (row.open && !isOpen(row.id))
        {
            row.open = false;
            closedDepth = row.depth;
        }
        result.push_back(row);
    }
    return result;
}

auto rowIndex(std::span<const HierarchyRow> rows, StableId id) -> std::optional<std::size_t>
{
    const auto row = std::ranges::find(rows, id, &HierarchyRow::id);
    return row == rows.end() ? std::nullopt : std::optional{ static_cast<std::size_t>(row - rows.begin()) };
}

auto ancestorIds(world::Registry& registry, world::Entity entity) -> std::vector<StableId>
{
    std::vector<StableId> ancestors;
    for (auto node = registry.get<Parent>(entity).entity; node; node = registry.get<Parent>(node).entity)
        ancestors.push_back(registry.get<StableId>(node));
    return ancestors;
}

auto rangeSelection(std::span<const HierarchyRow> rows, StableId from, StableId to) -> std::vector<StableId>
{
    const auto last = rowIndex(rows, to);
    if (!last)
        return {};
    const auto first = rowIndex(rows, from).value_or(*last);

    std::vector<StableId> range;
    const auto [low, high] = std::minmax(first, *last);
    for (auto index = low; index <= high; ++index)
        range.push_back(rows[index].id);
    if (first > *last)
        std::ranges::reverse(range);
    return range;
}

auto selectedRoots(world::Registry& registry, const Selection& selection) -> std::vector<world::Entity>
{
    const auto parentOf = [&registry](world::Entity entity) { return registry.has<Parent>(entity) ? registry.get<Parent>(entity).entity : world::Entity{}; };

    std::unordered_set<std::uint64_t> selected;
    for (const auto id : selection.ids())
        selected.insert(id.value);

    std::vector<world::Entity> roots;
    std::unordered_set<std::uint64_t> rootIds;
    std::unordered_set<std::uint64_t> ancestors; // Of the roots: the nodes to open to reach them in the rows
    for (const auto id : selection.ids())
    {
        const auto entity = registry.find(id);
        if (!entity)
            continue;
        std::vector<std::uint64_t> chain;
        bool covered = false;
        for (auto node = parentOf(entity); node && !covered; node = parentOf(node))
        {
            const auto value = registry.get<StableId>(node).value;
            covered = selected.contains(value);
            chain.push_back(value);
        }
        if (covered)
            continue;
        roots.push_back(entity);
        rootIds.insert(id.value);
        ancestors.insert(chain.begin(), chain.end());
    }
    if (roots.size() <= 1)
        return roots;

    // In the order of the rows, with only the roots' ancestors open
    roots.clear();
    for (const auto& row : hierarchyRows(registry, [&ancestors](StableId id) { return ancestors.contains(id.value); }))
    {
        if (rootIds.contains(row.id.value))
            roots.push_back(row.entity);
    }
    return roots;
}
}
