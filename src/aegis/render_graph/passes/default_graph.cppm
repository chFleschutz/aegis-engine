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

    /// @brief Where the scene passes read the frame scene; it must outlive the graph. The engine's is bound to
    ///        each frame's scene; a game's passes that read the scene take it from here too.
    const FrameSceneSource& scene;
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

/// @brief Adds the passes that render the scene into "Final": scene update, CPU- or GPU-driven geometry, lighting,
///        sky box, transparent (point lights), bloom and post-processing. Passes added after these and before
///        addOutputPasses draw onto "Final" before the frame shows it (a game's or the editor's overlays).
auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the passes that show "Final": UI (if requested) and present (not with a view).
auto addOutputPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the engine's standard passes: addScenePasses, then addOutputPasses. Compile the graph afterwards
///        (FrameGraph::compile, or GraphBuilder::plan without a device).
auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;
}
