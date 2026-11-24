export module aegis.world:transform_system;

import std;
import :registry;
import :system;

export namespace aegis::world::detail
{
/// @brief Composes the global transform of a child from its parent's global transform and its own local transform
/// @note The product of the matrices (parent * local), so the parent's rotation and scale apply to the child's
/// location, and the shear of a rotated child under a non-uniformly scaled parent is kept.
[[nodiscard]] auto compose(const GlobalTransform& parentGlobal, const Transform& local) -> GlobalTransform;

/// @brief Converts a world matrix into the local transform of an entity below the given parent (inverse of compose)
[[nodiscard]] auto toLocal(const GlobalTransform& parentGlobal, const math::Float4x4& world) -> Transform;
}

export namespace aegis::world
{
/// @brief Computes the global transforms from the hierarchy: every one at begin, then each frame the dynamic
///        entities' (parents first, so a dynamic child follows its parent in the same frame) and the static ones
///        marked with Registry::markStaticDirty
/// @note An entity that loses its DynamicTag gets its global transform once more, from where its Transform puts it.
class TransformSystem : public System
{
public:
    TransformSystem() = default;
    ~TransformSystem() = default;

    /// @brief After the scripts and gameplay that write the transforms
    [[nodiscard]] auto phase() const -> Phase override { return Phase::PostUpdate; }
    /// @brief Edited entities move in the editor too
    [[nodiscard]] auto runsInEdit() const -> bool override { return true; }

    /// @brief Starts listening for entities that lose their DynamicTag
    void onAttach(Registry& registry) override;
    void onDetach(Registry& registry) override;

    void onBegin(Registry& registry) override;

    void onUpdate(Registry& registry, float deltaSeconds) override;

private:
    struct Ranked
    {
        std::uint32_t depth; // Ancestors above the entity
        Entity entity;
    };

    void onDynamicDestroy(entt::registry& registry, entt::entity entity);

    // Recomputes the global transforms of the dynamic entities, parents before their children
    void updateDynamic(Registry& registry);

    // Recomputes the global transforms of the entities marked with Registry::markStaticDirty and of
    // their descendants from their parents' current ones, then invalidates the static set once more:
    // a consumer may have rebuilt it from the old values after the mark.
    static void updateStaticDirty(Registry& registry);

    static void updateSubtree(Registry& registry, Entity entity);

    std::vector<Entity> m_lostDynamic; // Entities whose DynamicTag went since the last update
    std::vector<Ranked> m_dynamic;     // Scratch for updateDynamic
};
}
