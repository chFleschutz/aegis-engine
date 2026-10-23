export module aegis.world.systems.camera_system;

import std;
import aegis.math;
import aegis.world.registry;
import aegis.world.entity;
import aegis.world.system;
import aegis.world.components;

export namespace aegis::world
{
class CameraSystem : public System
{
public:
    /// @brief After the transforms it reads, whatever the order they were added in
    [[nodiscard]] auto phase() const -> Phase override { return Phase::LateUpdate; }
    /// @brief Edited entities move in the editor too
    [[nodiscard]] auto runsInEdit() const -> bool override { return true; }

    virtual void onUpdate(Registry& registry, float deltaSeconds) override;

private:
    void calcViewMatrix(Camera& camera, GlobalTransform& transform);

    void calcPerspectiveProjection(Camera& camera);
};
}