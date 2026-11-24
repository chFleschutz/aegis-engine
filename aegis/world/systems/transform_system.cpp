module aegis.world;

import std;

namespace aegis::world::detail
{
auto compose(const GlobalTransform& parentGlobal, const Transform& local) -> GlobalTransform
{
    return { parentGlobal.matrix * local.matrix() };
}

auto toLocal(const GlobalTransform& parentGlobal, const math::Float4x4& world) -> Transform
{
    const auto [location, rotation, scale] = math::decomposeTRS(math::inverse(parentGlobal.matrix) * world);
    return { location, rotation, scale };
}
}

namespace aegis::world
{
void TransformSystem::onAttach(Registry& registry)
{
    registry.onDestroy<DynamicTag>().connect<&TransformSystem::onDynamicDestroy>(this);
}

void TransformSystem::onDetach(Registry& registry)
{
    registry.onDestroy<DynamicTag>().disconnect(this);
    m_lostDynamic.clear();
}

void TransformSystem::onDynamicDestroy(entt::registry&, entt::entity entity)
{
    // Also when the entity is destroyed: onUpdate skips it then.
    m_lostDynamic.emplace_back(entity);
}

void TransformSystem::onBegin(Registry& registry)
{
    m_lostDynamic.clear(); // begin computes them all

    // Calculate initial global transforms for all entities
    std::vector<Entity> hierarchy;
    auto view = registry.view<GlobalTransform>();
    for (auto&& [entity, globalTransform] : view.each())
    {
        // Find hierarchy of all parents
        hierarchy.clear();
        Entity current = Entity{ entity };
        while (current)
        {
            hierarchy.emplace_back(current);
            current = registry.get<Parent>(current).entity;
        }

        globalTransform = GlobalTransform{};
        for (auto e : std::ranges::views::reverse(hierarchy))
        {
            globalTransform = detail::compose(globalTransform, registry.get<Transform>(e));
        }
    }
}

void TransformSystem::onUpdate(Registry& registry, float deltaSeconds)
{
    // An entity that became static is where its Transform puts it now, which may be past its last global transform
    for (const auto entity : m_lostDynamic)
    {
        if (registry.valid(entity) && !registry.has<DynamicTag>(entity))
            registry.markStaticDirty(entity);
    }
    m_lostDynamic.clear();

    updateStaticDirty(registry);
    updateDynamic(registry);
}

void TransformSystem::updateDynamic(Registry& registry)
{
    m_dynamic.clear();
    for (auto&& [entity, transform, globalTransform, parent] : registry.view<Transform, GlobalTransform, Parent, DynamicTag>().each())
    {
        std::uint32_t depth = 0;
        for (Entity ancestor = parent.entity; ancestor; ancestor = registry.get<Parent>(ancestor).entity)
            ++depth;
        m_dynamic.push_back({ depth, Entity{ entity } });
    }
    std::ranges::sort(m_dynamic, {}, &Ranked::depth);

    for (const auto& [depth, entity] : m_dynamic)
    {
        const Entity parent = registry.get<Parent>(entity).entity;
        auto& globalTransform = registry.get<GlobalTransform>(entity);
        if (!parent || !registry.has<GlobalTransform>(parent))
            globalTransform.matrix = registry.get<Transform>(entity).matrix();
        else
            globalTransform = detail::compose(registry.get<GlobalTransform>(parent), registry.get<Transform>(entity));
    }
}

void TransformSystem::updateStaticDirty(Registry& registry)
{
    std::vector<Entity> dirty;
    for (auto&& [entity] : registry.view<StaticDirtyTag>().each())
        dirty.emplace_back(Entity{ entity });
    if (dirty.empty())
        return;

    for (const auto entity : dirty)
    {
        updateSubtree(registry, entity);
        registry.remove<StaticDirtyTag>(entity);
    }
    registry.invalidateStatic();
}

void TransformSystem::updateSubtree(Registry& registry, Entity entity)
{
    if (!registry.has<Transform, GlobalTransform, Parent, Children>(entity))
        return;

    const Entity parent = registry.get<Parent>(entity).entity;
    const GlobalTransform parentGlobal =
        parent && registry.has<GlobalTransform>(parent) ? registry.get<GlobalTransform>(parent) : GlobalTransform{};
    registry.get<GlobalTransform>(entity) = detail::compose(parentGlobal, registry.get<Transform>(entity));

    for (Entity child = registry.get<Children>(entity).first; child; child = registry.get<Siblings>(child).next)
        updateSubtree(registry, child);
}
}
