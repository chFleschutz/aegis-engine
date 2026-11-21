export module aegis.world:camera_system;

import std;
import aegis.math;
import :registry;
import :entity;
import :system;
import :components;

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
    void calcViewMatrix(CameraMatrices& matrices, GlobalTransform& transform);

    void calcPerspectiveProjection(CameraMatrices& matrices, const Camera& camera);
};
}