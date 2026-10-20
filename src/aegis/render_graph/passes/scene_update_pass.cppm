export module aegis.render_graph:scene_update_pass;
import std;
import :gpu_types;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
import aegis.math;
import aegis.world;

export namespace aegis::render_graph
{
/// @brief Writes per-frame scene data the other passes read: "CameraData" (a per-frame buffer of
///        CameraData).
class SceneUpdatePass : public RenderPass
{
public:
    explicit SceneUpdatePass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceId m_cameraData;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The CameraData the shaders read for a camera whose view and projection matrices are current
///        (the camera system computes them), at the world position of 'transform'.
[[nodiscard]] auto makeCameraData(const aegis::Camera& camera, const aegis::GlobalTransform& transform) -> CameraData;
}
