export module aegis.render_graph:render_system;
import std;
import :error;
import aegis.rhi;
import aegis.world;

export namespace aegis::render_graph
{
/// @brief What a render system records with, inside its pass's rendering.
struct RenderContext
{
    const rhi::CommandBuffer& cmd;
    aegis::world::World& scene;
    std::uint32_t frameIndex;
    rhi::DeviceAddress camera; // This frame's CameraData
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

    virtual auto render(const RenderContext& context) -> void = 0;
};
}
