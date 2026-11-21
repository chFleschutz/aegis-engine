export module aegis.render_passes:default_graph;
import std;
import :frame_scene;
import :gpu_driven;
import :lighting_pass;
import :names;
import :shadows;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
struct DefaultPassesDesc
{
    /// @brief A view the graph renders (FrameScene::View): its scene passes are declared in a frame graph scope of
    ///        its name, so its resources are "<name>/Depth" and so on (scopedName).
    struct View
    {
        std::string name;
        /// @brief The extent its textures follow (the scope's); none: the output's.
        std::optional<rhi::Extent2D> extent;
        /// @brief Which passes render it (addScenePasses): the lit scene, or a light's depth.
        ViewKind kind{ ViewKind::Main };
        /// @brief A main view's: the index in 'views' of the shadow view its lighting samples (addShadowViews).
        std::optional<std::uint32_t> shadow;
    };

    /// @brief The views render into targets of their own, which the UI shows: the frame's target shows only the UI,
    ///        and there is no present pass.
    struct ShownViews
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
    /// @brief The views, in the order of FrameScene::views; at least one main view. Without shadow views (see
    ///        addShadowViews) the main views are lit unshadowed.
    std::vector<View> views{ View{ .name = std::string{ names::mainView } } };
    /// @brief The index in 'views' (and FrameScene::views) of the view whose scene passes are being declared; set
    ///        before each view's passes (addDefaultPasses, the engine).
    std::uint32_t view{ 0 };
    /// @brief The pass writing the instances all views share (addSharedPasses), which the GPU-driven scene passes
    ///        read; none: addScenePasses adds one of its own to the view.
    const InstanceUpdatePass* instances{ nullptr };
    /// @brief The pass writing the lights all views share (addSharedPasses), which the lighting passes read; none:
    ///        addScenePasses adds one of its own to the view.
    const LightUpdatePass* lights{ nullptr };
    /// @brief Render the views into targets of their own (see ShownViews): the UI pass reads each view's "Final" and
    ///        draws onto the frame's target. Implies ui. Without it, the first view's "Final" is presented.
    std::optional<ShownViews> shownViews;
};

/// @brief The passes addSharedPasses added, for the DefaultPassesDesc of the views' scene passes.
struct SharedPasses
{
    const InstanceUpdatePass* instances{ nullptr }; // For DefaultPassesDesc::instances; none without gpuDriven
    const LightUpdatePass* lights{ nullptr };       // For DefaultPassesDesc::lights
};

/// @brief Appends a shadow view (ViewKind::Shadow, shadowViewName, shadowMapExtent) for each main view of 'views' that
///        has none, and points the main view at it, as addShadowViews(FrameScene&) does for the frame scene's views.
auto addShadowViews(std::vector<DefaultPassesDesc::View>& views) -> void;

/// @brief Adds the passes all views share, outside any scope: the LightUpdatePass writing the frame's lights and,
///        with gpuDriven, the InstanceUpdatePass writing its instances and draw batches.
auto addSharedPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> SharedPasses;

/// @brief Adds the passes that render the view desc.view: for a main view, into "Final": scene update, light update
///        (without desc.lights), CPU- or GPU-driven geometry, lighting (sampling its shadow view's "Depth"), sky box,
///        transparent (point lights), bloom and post-processing; for a shadow view, addShadowPasses. Call it inside the
///        view's scope (GraphBuilder::beginScope). Passes added after a main view's and before addOutputPasses draw
///        onto "Final" before the frame shows it (a game's or the editor's overlays).
auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the passes that render the shadow view desc.view's "Depth" (its shadow map): scene update, then the
///        CPU-driven geometry pass or culling and the GPU-driven geometry pass, with Technique::Depth. Call it inside
///        the view's scope.
auto addShadowPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the passes that show the main views' "Final", outside any scope: UI (if requested) and present (not with
///        shownViews).
auto addOutputPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the engine's standard passes: addSharedPasses, then addScenePasses in each view's scope, then
///        addOutputPasses. Compile the graph afterwards (FrameGraph::compile, or GraphBuilder::plan
///        without a device).
auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;
}
