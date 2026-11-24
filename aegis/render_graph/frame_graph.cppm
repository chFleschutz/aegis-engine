export module aegis.render_graph:frame_graph;
import std;
import :declarations;
import :error;
import :graph_builder;
import :render_pass;
import :resource_pool;
import :schedule;
import aegis.renderer;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief How long one pass took: on the GPU, and recording it on the CPU.
struct PassTiming
{
    std::string name;
    double gpuMs{ 0.0 };
    double cpuMs{ 0.0 };
};

/// @brief A pass in the compiled graph, as tools show it.
struct PassDescription
{
    RenderPass* pass;
    std::string_view name;
    std::vector<ResourceId> reads;
    std::vector<ResourceId> writes;
};

/// @brief A compiled graph: the passes and declarations of a finished GraphBuilder, the resources they declare,
///        and the recording of each frame with the barriers between passes. Nothing can be added to it.
class FrameGraph
{
public:
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph(FrameGraph&&) = delete;
    /// @brief Frees every texture and buffer of the graph (through the pool). Wait for the GPU first if frames
    ///        recorded with it may still be running.
    ~FrameGraph() = default;

    auto operator=(const FrameGraph&) -> FrameGraph& = delete;
    auto operator=(FrameGraph&&) -> FrameGraph& = delete;

    /// @brief Takes over the passes and declarations of 'builder' (moved from: don't use it again), resolves
    ///        references, orders the passes, creates every resource, lets each pass create its own
    ///        (RenderPass::createResources), and plans the barriers.
    /// @param output The extent of textures that follow the output (usually renderer::Renderer::extent()).
    [[nodiscard]] static auto create(rhi::Device& device, GraphBuilder&& builder, rhi::Extent2D output)
        -> std::expected<std::unique_ptr<FrameGraph>, Error>;

    /// @brief Call after renderer::Renderer::resize with its new extent: recreates the textures that follow
    ///        the output. They keep their handles, but passes may hold views of the replaced images.
    [[nodiscard]] auto resized(rhi::Extent2D output) -> std::expected<void, Error>;
    /// @brief Gives the scope 'scope' (ResourceDeclarations::beginScope) the extent 'extent': recreates its textures
    ///        that follow it, as resized does for the output's. The GPU must be idle.
    /// @return InvalidGraph for a scope the graph doesn't have.
    [[nodiscard]] auto resized(std::string_view scope, rhi::Extent2D extent) -> std::expected<void, Error>;

    /// @brief Sets the image behind an imported texture ('resource' from ResourceDeclarations::importTexture or
    ///        find), in the state it was declared to be in when a frame starts. Call after create and before
    ///        the first execute; it stays bound until bound again. The graph never frees it.
    auto bind(ResourceId resource, const renderer::Texture& texture) -> void { m_pool.bind(resource, texture); }
    /// @brief Sets the buffer behind an imported buffer, like the texture overload.
    auto bind(ResourceId resource, rhi::BufferHandle buffer) -> void { m_pool.bind(resource, buffer); }

    /// @brief Lets every pass create what recording the coming frame needs (RenderPass::prepare). Call before each
    ///        execute, once what the passes read for that frame is in place; a pass not prepared for a draw batch
    ///        skips it.
    /// @return The first pass's error, its message prefixed with the pass name; the passes after it are not
    ///         prepared then, and the frame must not be executed.
    [[nodiscard]] auto prepare() -> std::expected<void, Error>;

    /// @brief Records every pass, in order, into frameInfo.cmd, then the barriers that leave imported
    ///        resources as declared. Afterwards persistent textures and history pairs are no longer fresh, and
    ///        the history pairs swap.
    auto execute(const FrameInfo& frameInfo) -> void;

    [[nodiscard]] auto pool() noexcept -> ResourcePool& { return m_pool; }

    [[nodiscard]] auto declarations() const noexcept -> const ResourceDeclarations& { return m_declarations; }
    /// @brief The passes in execution order.
    [[nodiscard]] auto passes() const -> std::vector<RenderPass*>;
    /// @brief The same passes with what each reads and writes, for tools.
    [[nodiscard]] auto describePasses() const -> std::vector<PassDescription>;

    /// @brief Per-pass timings of the most recent frame the GPU has finished, in execution order.
    ///        Empty until the first frame slot comes round again.
    [[nodiscard]] auto timings() const noexcept -> std::span<const PassTiming> { return m_timings; }

private:
    FrameGraph(rhi::Device& device, ResourceDeclarations&& declarations, std::vector<std::unique_ptr<RenderPass>>&& passes,
        rhi::Extent2D output);

    [[nodiscard]] auto build() -> std::expected<void, Error>;
    [[nodiscard]] auto resizedPasses() -> std::expected<void, Error>;
    auto recordTransitions(const rhi::CommandBuffer& cmd, const std::vector<detail::Transition>& transitions) const -> void;

    ResourceDeclarations m_declarations;               // Before m_pool, which refers to it
    std::vector<std::unique_ptr<RenderPass>> m_passes; // In the order added
    ResourcePool m_pool;
    std::vector<std::uint32_t> m_order;                         // Execution order, indices into m_passes
    std::vector<std::vector<detail::Transition>> m_transitions; // Per entry of m_order
    std::vector<detail::Transition> m_endOfFrame;               // After the last pass
    std::vector<detail::ResourceKind> m_kinds;                  // Per pool entry

    // GPU timestamps around every pass: one pool per frame in flight, read once its frame is done.
    struct FrameTimer
    {
        rhi::QueryPool pool;
        std::vector<double> cpuMs; // Recording time per pass, kept until the GPU times arrive
        bool pending{ false };
    };
    auto readTimings(FrameTimer& timer) -> void;

    std::vector<FrameTimer> m_timers;
    std::vector<PassTiming> m_timings;
};
}
