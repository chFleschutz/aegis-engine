export module aegis.render_graph:render_pass;
import std;
import :declarations;
import :error;
import :resource;
import :resource_pool;
import aegis.reflect;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief What a pass records a frame with. FrameGraph::execute fills it per pass.
struct FrameInfo
{
    const rhi::CommandBuffer& cmd;
    /// @brief The frame slot being recorded, for per-frame data the pass keeps itself. The pool's per-frame buffers
    ///        already pick it (ResourcePool::bufferAddress).
    std::uint32_t frameIndex;
    /// @brief The extent of the executing pass's scope (ResourcePool::scopeExtent): the scope's own, else the output's;
    ///        an unscoped pass gets the output's. A texture with an extent of its own (TextureDesc::extent) has that
    ///        instead (ResourcePool::extent).
    rhi::Extent2D extent;
};

/// @brief A pass's runtime tunables: 'object', the instance of 'type' (registered with aegis.reflect in the Settings
///        category) the pass reads every frame, which may outlive the pass and be shared with other passes (the
///        engine's RenderSettings). Tools edit it through the type's fields.
struct PassSettings
{
    const reflect::TypeInfo& type;
    void* object;
};

/// @brief One node of the frame graph. The constructor declares resources on the ResourceDeclarations.
///
///        Everything that can fail (pipelines, shaders, buffer growth) runs in createResources, or in prepare when
///        it depends on what a frame draws, and returns its error; execute only records, into what those created.
///        A pass never logs a failure in execute and draws less: an error from prepare ends the frame loop
///        (Engine::run), so a test run fails rather than shows a wrong image.
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

    /// @brief Creates what the pass owns. Called by FrameGraph::create once graph resources exist.
    [[nodiscard]] virtual auto createResources(ResourcePool& /*pool*/) -> std::expected<void, Error> { return {}; }

    /// @brief Creates what recording the coming frame needs and the pass lacks, such as the pipelines of the draw
    ///        batches it draws, or a larger buffer for its instances. Called by FrameGraph::prepare for every pass,
    ///        before the frame is recorded: it has no command buffer, and only creates.
    [[nodiscard]] virtual auto prepare(ResourcePool& /*pool*/) -> std::expected<void, Error> { return {}; }

    /// @brief Records the pass. The graph has already recorded the barriers its accesses need. Cannot fail: what
    ///        could was done in prepare.
    virtual auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void = 0;

    /// @brief The pass's runtime tunables, or none for a pass without any. The editor's renderer panel shows them.
    [[nodiscard]] virtual auto settings() -> std::optional<PassSettings> { return std::nullopt; }
};
}
