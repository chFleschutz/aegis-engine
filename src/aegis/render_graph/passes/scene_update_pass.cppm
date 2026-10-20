export module aegis.render_graph:scene_update_pass;
import std;
import :gpu_types;
import :render_pass;
import :resource;
import :declarations;
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
    explicit SceneUpdatePass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceHandle m_cameraData;
};
}
