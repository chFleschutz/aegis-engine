export module aegis.scene.systems.transform_system;

import std;
import aegis.scene.registry;
import aegis.scene.system;

export namespace aegis::scene::detail
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

export namespace aegis::scene
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
};
}