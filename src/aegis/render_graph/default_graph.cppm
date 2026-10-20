export module aegis.render_graph:default_graph;
import std;
import :draw_batch_registry;
import :graph_builder;
import aegis.rhi;

export namespace aegis::render_graph
{
struct DefaultPassesDesc
{
    /// @brief The mesh-shader geometry path; needs rhi::Device::Capabilities::meshShaders.
    bool gpuDriven;
    /// @brief Read by the GPU-driven passes; must outlive the graph.
    const DrawBatchRegistry& drawBatches;
    /// @brief Draws ImGui over the frame when set (see UiPass::Desc::build).
    std::function<void()> buildUi{};
    /// @brief Needed with buildUi to compile the graph (ImGui's backend takes the instance); declaring it needs none.
    const rhi::Context* context{ nullptr };
};

/// @brief Adds the engine's standard passes: scene update, CPU- or GPU-driven geometry, lighting,
///        sky box, transparent (point lights), bloom, post-processing, UI (if requested) and present.
///        Compile the graph afterwards (FrameGraph::compile, or GraphBuilder::plan without a device).
auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;
}
