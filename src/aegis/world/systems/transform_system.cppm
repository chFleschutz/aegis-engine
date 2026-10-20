export module aegis.world.systems.transform_system;

import std;
import aegis.world.registry;
import aegis.world.system;

export namespace aegis::world::detail
{
/// @brief Composes the global transform of a child from its parent's global transform and its own local transform
/// @note Goes through matrices, so the parent's rotation and scale apply to the child's location. A non-uniform
/// parent scale combined with a child rotation introduces shear, which TRS cannot represent (it is dropped).
[[nodiscard]] inline auto compose(const GlobalTransform& parentGlobal, const Transform& local) -> GlobalTransform
{
    GlobalTransform global;
    math::decomposeTRS(parentGlobal.matrix() * local.matrix(), global.location, global.rotation, global.scale);
    return global;
}

/// @brief Converts a world matrix into the local transform of an entity below the given parent (inverse of compose)
[[nodiscard]] inline auto toLocal(const GlobalTransform& parentGlobal, const glm::mat4& world) -> Transform
{
    Transform local;
    math::decomposeTRS(glm::inverse(parentGlobal.matrix()) * world, local.location, local.rotation, local.scale);
    return local;
}
}

export namespace aegis::world
{
class TransformSystem : public System
{
public:
    TransformSystem() = default;
    ~TransformSystem() = default;

    void onBegin(Registry& registry) override
    {
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

    void onUpdate(Registry& registry, float deltaSeconds) override
    {
        updateStaticDirty(registry);

        // Note: This lags 1 frame per parent behind the actual local transform (but this is fine :)
        auto view = registry.view<Transform, GlobalTransform, Parent, DynamicTag>();
        for (auto&& [entity, transform, globalTransform, parent] : view.each())
        {
            if (!parent.entity || !registry.has<GlobalTransform>(parent.entity))
            {
                globalTransform.location = transform.location;
                globalTransform.rotation = transform.rotation;
                globalTransform.scale = transform.scale;
                continue;
            }

            auto& parentGlobal = registry.get<GlobalTransform>(parent.entity);
            globalTransform = detail::compose(parentGlobal, transform);
        }
    }

private:
    // Recomputes the global transforms of the entities marked with Registry::markStaticDirty and of
    // their descendants from their parents' current ones, then invalidates the static set once more:
    // a consumer may have rebuilt it from the old values after the mark.
    static void updateStaticDirty(Registry& registry)
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

    static void updateSubtree(Registry& registry, Entity entity)
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
};
}
