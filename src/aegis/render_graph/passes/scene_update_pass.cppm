export module aegis.render_passes:scene_update_pass;
import std;
import :gpu_types;
import aegis.render_graph;

export namespace aegis::render_graph
{
/// @brief Writes per-frame scene data the other passes read: "CameraData" (a per-frame buffer of
///        CameraData).
class SceneUpdatePass : public RenderPass
{
public:
    SceneUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    ResourceId m_cameraData;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The CameraData the shaders read for the frame's camera.
[[nodiscard]] auto makeCameraData(const FrameScene::Camera& camera) -> CameraData;
}
