module;
#include <cstdint>

export module aegis.render_graph:scene_update_pass;
import :frustum;
import :render_pass;
import :resource;
import :resource_pool;
import Aegis.Math;
import Aegis.Scene;

export namespace aegis::render_graph
{
/// @brief The main camera as shaders read it (common::Camera). Matrices are row-major.
struct CameraData
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    Frustum frustum;
    glm::vec3 position;
};

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
