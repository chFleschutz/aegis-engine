export module aegis.render_passes:default_graph;
import std;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
struct DefaultPassesDesc
{
    /// @brief The scene renders into a view's own target instead of the frame's: the graph's output extent is
    ///        the view's, and "Final" is the view's target, left for the UI to show.
    struct View
    {
        /// @brief The frame's target format (renderer::Renderer::targetFormat()), which the UI draws onto.
        rhi::Format targetFormat;
    };

    /// @brief The mesh-shader geometry path; needs rhi::Device::Capabilities::meshShaders.
    bool gpuDriven;
    /// @brief Draws ImGui over the frame: the draw data of the frame the caller ended (see UiPass).
    bool ui{ false };
    /// @brief Needed with ui to compile the graph (ImGui's backend takes the instance); declaring it needs none.
    const rhi::Context* context{ nullptr };
    /// @brief Render into a view (see View): the UI pass reads "Final" and draws onto the frame's target,
    ///        and there is no present pass. Implies ui.
    std::optional<View> view;
};

/// @brief Adds the engine's standard passes: scene update, CPU- or GPU-driven geometry, lighting,
///        sky box, transparent (point lights), bloom, post-processing, UI (if requested) and present
///        (not with a view). Compile the graph afterwards (FrameGraph::compile, or GraphBuilder::plan without a device).
auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;
}
