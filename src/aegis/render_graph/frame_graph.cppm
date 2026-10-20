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
import aegis.world;

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

/// @brief A GraphBuilder with a device behind it: creates the resources the passes declare, and records each
///        frame with the barriers between passes.
class FrameGraph : public GraphBuilder
{
public:
    /// @param output The extent of textures that follow the output (usually renderer::Renderer::extent()).
    FrameGraph(rhi::Device& device, rhi::Extent2D output);
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph(FrameGraph&&) = delete;
    /// @brief Frees every texture and buffer of the graph (through the pool). Wait for the GPU first if frames
    ///        recorded with it may still be running.
    ~FrameGraph() = default;

    auto operator=(const FrameGraph&) -> FrameGraph& = delete;
    auto operator=(FrameGraph&&) -> FrameGraph& = delete;

    /// @brief Resolves references, orders the passes, creates every resource, and plans the barriers.
    ///        Call once, after all passes were added.
    [[nodiscard]] auto compile() -> std::expected<void, Error>;

    /// @brief Call after renderer::Renderer::resize with its new extent: recreates the textures that follow
    ///        the output. They keep their handles, but passes may hold views of the replaced images.
    [[nodiscard]] auto resized(rhi::Extent2D output) -> std::expected<void, Error>;

    /// @brief Tells every pass a scene was loaded (e.g. to upload static instance data).
    auto sceneInitialized(aegis::world::World& scene) -> void;

    /// @brief Records every pass, in order, into frameInfo.cmd.
    auto execute(const FrameInfo& frameInfo) -> void;

    [[nodiscard]] auto pool() noexcept -> ResourcePool& { return m_pool; }

    /// @brief The passes in execution order (valid after compile).
    [[nodiscard]] auto passes() const -> std::vector<RenderPass*>;
    /// @brief The same passes with what each reads and writes, for tools.
    [[nodiscard]] auto describePasses() const -> std::vector<PassDescription>;

    /// @brief Per-pass timings of the most recent frame the GPU has finished, in execution order.
    ///        Empty until the first frame slot comes round again.
    [[nodiscard]] auto timings() const noexcept -> std::span<const PassTiming> { return m_timings; }

private:
    auto recordTransitions(const rhi::CommandBuffer& cmd, const std::vector<detail::Transition>& transitions) const -> void;

    ResourcePool m_pool;
    std::vector<std::uint32_t> m_order;                         // Execution order, indices into m_passes
    std::vector<std::vector<detail::Transition>> m_transitions; // Per entry of m_order
    std::vector<detail::ResourceKind> m_kinds;                  // Per pool entry
    bool m_compiled{ false };

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
