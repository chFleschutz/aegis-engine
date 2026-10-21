export module aegis.render_graph:render_pass;
import std;
import :declarations;
import :error;
import :frame_scene;
import :resource;
import :resource_pool;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief What a pass records a frame with.
struct FrameInfo
{
    const rhi::CommandBuffer& cmd;
    /// @brief What to draw: instances, camera, lights and environment. Passes read these here, not from the world.
    const FrameScene& frameScene;
    std::uint32_t frameIndex;
    rhi::Extent2D extent;
    /// @brief The renderer's target for this frame (swapchain image or offscreen target), in
    ///        ResourceState::ColorAttachment; a pass using it must leave it in that state.
    rhi::ImageViewHandle target;
};

/// @brief One node of the frame graph. The constructor declares resources on the ResourceDeclarations;
///        everything that can fail (pipelines, shaders) belongs in createResources.
class RenderPass
{
public:
    RenderPass() = default;
    RenderPass(const RenderPass&) = delete;
    RenderPass(RenderPass&&) = delete;
    virtual ~RenderPass() = default;

    auto operator=(const RenderPass&) -> RenderPass& = delete;
    auto operator=(RenderPass&&) -> RenderPass& = delete;

    /// @brief The pass name, for tools and logs. What the pass reads and writes follows from its declarations
    ///        (ResourceDeclarations::reads, writes): each access is declared once, in the constructor.
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// @brief Creates what the pass owns. Called by FrameGraph::compile once graph resources exist.
    [[nodiscard]] virtual auto createResources(ResourcePool& /*pool*/) -> std::expected<void, Error> { return {}; }

    /// @brief Called by FrameGraph::resized after the graph recreated its output-sized
    ///        textures, for a pass to recreate what it derived from them (e.g. extra image views).
    [[nodiscard]] virtual auto resized(ResourcePool& /*pool*/) -> std::expected<void, Error> { return {}; }

    /// @brief Records the pass. The graph has already recorded the barriers its accesses need.
    virtual auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void = 0;

    /// @brief Settings for the editor's renderer panel (ImGui).
    virtual auto drawUI() -> void {}
};
}
