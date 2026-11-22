export module aegis.render_passes:render_system;
import std;
import :frame_scene;
import aegis.render_graph;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief What a render system records with, inside its pass's rendering.
struct RenderContext
{
    const rhi::CommandBuffer& cmd;
    const FrameScene& frameScene;
    std::uint32_t frameIndex;
    rhi::DeviceAddress camera;        // This frame's CameraData
    rhi::DeviceAddress lighting{ 0 }; // This frame's LightingUniforms, in a pass that lights what it draws (ForwardPass)
    /// @brief The view matrix of the pass's camera (FrameScene::Camera::view), which blended draws sort by
    ///        (detail::sortBackToFront); identity without a camera.
    glm::mat4 view{ 1.0f };
};

/// @brief The attachments a pass renders into, which a render system's pipelines must match.
struct RenderTargets
{
    std::span<const rhi::Pipeline::ColorAttachment> colorAttachments;
    rhi::Format depthAttachment{ rhi::Format::Unknown };
};

/// @brief Draws one kind of scene object inside a graphics pass.
class RenderSystem
{
public:
    RenderSystem() = default;
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) = delete;
    virtual ~RenderSystem() = default;

    auto operator=(const RenderSystem&) -> RenderSystem& = delete;
    auto operator=(RenderSystem&&) -> RenderSystem& = delete;

    /// @brief Creates the system's pipelines for its pass's attachments.
    [[nodiscard]] virtual auto createResources(rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> = 0;

    /// @brief Creates what drawing 'frameScene' needs and the system lacks (RenderPass::prepare).
    [[nodiscard]] virtual auto prepare(const FrameScene& /*frameScene*/) -> std::expected<void, Error> { return {}; }

    virtual auto render(const RenderContext& context) -> void = 0;
};
}
