export module aegis.world.systems.transform_system;

import std;
import aegis.world.registry;
import aegis.world.system;

export namespace aegis::world::detail
{
/// @brief Composes the global transform of a child from its parent's global transform and its own local transform
/// @note Goes through matrices, so the parent's rotation and scale apply to the child's location. A non-uniform
/// parent scale combined with a child rotation introduces shear, which TRS cannot represent (it is dropped).
[[nodiscard]] auto compose(const GlobalTransform& parentGlobal, const Transform& local) -> GlobalTransform;

/// @brief Converts a world matrix into the local transform of an entity below the given parent (inverse of compose)
[[nodiscard]] auto toLocal(const GlobalTransform& parentGlobal, const glm::mat4& world) -> Transform;
}

export namespace aegis::world
{
class TransformSystem : public System
{
public:
    TransformSystem() = default;
    ~TransformSystem() = default;

    /// @brief After the scripts and gameplay that write the transforms
    [[nodiscard]] auto phase() const -> Phase override { return Phase::PostUpdate; }
    /// @brief Edited entities move in the editor too
    [[nodiscard]] auto runsInEdit() const -> bool override { return true; }

    void onBegin(Registry& registry) override;

    void onUpdate(Registry& registry, float deltaSeconds) override;

private:
    // Recomputes the global transforms of the entities marked with Registry::markStaticDirty and of
    // their descendants from their parents' current ones, then invalidates the static set once more:
    // a consumer may have rebuilt it from the old values after the mark.
    static void updateStaticDirty(Registry& registry);

    static void updateSubtree(Registry& registry, Entity entity);
};
}
