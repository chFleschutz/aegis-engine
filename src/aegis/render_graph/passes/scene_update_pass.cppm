export module aegis.render_graph:scene_update_pass;
import std;
import :gpu_types;
import :render_pass;
import :resource;
import :resource_pool;
import aegis.math;
import aegis.scene;

export namespace aegis::render_graph
{
/// @brief Writes per-frame scene data the other passes read: "CameraData" (a per-frame buffer of
///        CameraData).
class SceneUpdatePass : public RenderPass
{
public:
    explicit SceneUpdatePass(ResourcePool& pool);

    [[nodiscard]] auto info() const -> Info override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceHandle m_cameraData;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The CameraData the shaders read for a camera whose view and projection matrices are current
///        (the camera system computes them), at the world position of 'transform'.
[[nodiscard]] auto makeCameraData(const aegis::Camera& camera, const aegis::GlobalTransform& transform) -> CameraData;
}
