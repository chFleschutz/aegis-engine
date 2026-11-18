export module aegis.render_passes:scene_update_pass;
import std;
import :frame_scene;
import :gpu_types;
import aegis.render_graph;

export namespace aegis::render_graph
{
/// @brief Writes per-frame scene data the other passes read: "CameraData" (a per-frame buffer of
///        CameraData), for the camera of one view (FrameScene::views).
class SceneUpdatePass : public RenderPass
{
public:
    /// @param view The index of the view in FrameScene::views whose camera it writes.
    SceneUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view = 0);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    std::uint32_t m_view;
    ResourceId m_cameraData;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The CameraData the shaders read for the frame's camera.
[[nodiscard]] auto makeCameraData(const FrameScene::Camera& camera) -> CameraData;
}
