export module aegis.render_passes:default_graph;
import std;
import :frame_scene;
import :gpu_driven;
import :lighting_pass;
import :names;
import :pass_settings;
import :pipeline_library;
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
        /// @brief A main view's: the index in 'views' of the first of its maxShadowCascades shadow views, which follow
        ///        one another and its lighting samples (addShadowViews).
        std::optional<std::uint32_t> shadow;
        /// @brief A spot shadow view's: the tile of "SpotShadowAtlas" it draws into (spotShadowTile); none: it draws
        ///        into its own "Depth".
        std::optional<rhi::Rect2D> tile;
    };

    /// @brief Where the scene passes read the frame scene; it must outlive the graph. The engine's is bound to
    ///        each frame's scene; a game's passes that read the scene take it from here too.
    const FrameSceneSource& scene;
    /// @brief Where the passes take their pipelines and the BRDF lookup table from; it must outlive the graph. The
    ///        engine's outlives all its graphs, so every view and every rebuilt graph shares one set of pipelines.
    PipelineLibrary& pipelines;
    /// @brief The pass settings the passes read while recording; it must outlive the graph. The engine's outlives all
    ///        its graphs, so a rebuild keeps what the renderer panel set and every view reads the same.
    RenderSettings& settings;
    /// @brief The mesh-shader geometry path; needs rhi::Device::Capabilities::meshShaders.
    bool gpuDriven;
    /// @brief The format of the frame's target (renderer::Renderer::targetFormat()), names::target: the present pass
    ///        copies onto it, or the UI draws onto it (shownViews).
    rhi::Format targetFormat;
    /// @brief Draws ImGui over the frame: the draw data of the frame the caller ended (see UiPass).
    bool ui{ false };
    /// @brief Needed with ui to compile the graph: the UI pass records through it. It belongs to the caller and outlives
    ///        the graph; declaring the graph needs none.
    rhi::ImGuiBackend* uiBackend{ nullptr };
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
    /// @brief Render the views into targets of their own, which the UI shows: the UI pass reads each view's "Final"
    ///        and draws onto the frame's target, and there is no present pass. Implies ui. Without it, the first
    ///        view's "Final" is presented.
    bool shownViews{ false };

    /// @brief Whether the frame's target encodes on write (an sRGB format), so post-processing leaves "Final" linear.
    [[nodiscard]] auto targetEncodes() const noexcept -> bool
    {
        return targetFormat == rhi::Format::BGRA8_SRGB || targetFormat == rhi::Format::RGBA8_SRGB;
    }
};

/// @brief The passes addSharedPasses added, for the DefaultPassesDesc of the views' scene passes.
struct SharedPasses
{
    const InstanceUpdatePass* instances{ nullptr }; // For DefaultPassesDesc::instances; none without gpuDriven
    const LightUpdatePass* lights{ nullptr };       // For DefaultPassesDesc::lights
};

/// @brief Appends maxShadowCascades shadow views (ViewKind::Shadow, shadowViewName, shadowMapExtent) for each main view
///        of 'views' that has none, and points the main view at the first; then, once, the maxShadowedSpotLights spot
///        shadow views (spotShadowViewName, spotShadowTileExtent, spotShadowTile), as addShadowViews(FrameScene&) does
///        for the frame scene's views.
auto addShadowViews(std::vector<DefaultPassesDesc::View>& views) -> void;

/// @brief The order to declare the views' passes in: the indices of 'views', the spot shadow views (View::tile) first,
///        then the others in their order. A reference is ordered after the writes declared before it, so the spot
///        shadow views must draw their tiles of the shared "SpotShadowAtlas" before the main views' lighting reads it.
[[nodiscard]] auto viewDeclarationOrder(std::span<const DefaultPassesDesc::View> views) -> std::vector<std::uint32_t>;

/// @brief Adds the passes all views share, outside any scope: the LightUpdatePass writing the frame's lights, the
///        SpotShadowAtlasPass creating "SpotShadowAtlas" when a view draws into it (View::tile) and, with gpuDriven,
///        the InstanceUpdatePass writing its instances and draw batches.
auto addSharedPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> SharedPasses;

/// @brief Adds the passes that render the view desc.view: for a main view, into "Final": scene update, light update
///        (without desc.lights), CPU- or GPU-driven geometry, lighting (sampling its shadow views' "Depth"), sky box,
///        forward (blended materials, CPU-driven on both paths), transparent (point lights), bloom and post-processing; for a shadow view, addShadowPasses. Its lighting samples
///        "SpotShadowAtlas" when its view has shadow views and a view draws into the atlas. Call it inside the
///        view's scope (GraphBuilder::beginScope). Passes added after a main view's and before addOutputPasses draw
///        onto "Final" before the frame shows it (a game's or the editor's overlays).
auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the passes that render the shadow view desc.view's "Depth" (its shadow map), or its tile of
///        "SpotShadowAtlas" (View::tile): scene update, then the CPU-driven geometry pass or culling and the GPU-driven
///        geometry pass, with Technique::Depth. Call it inside the view's scope.
auto addShadowPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the passes that show the main views' "Final", outside any scope: UI (if requested) and present (not with
///        shownViews).
auto addOutputPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;

/// @brief Adds the engine's standard passes: addSharedPasses, then addScenePasses in each view's scope (in
///        viewDeclarationOrder: the spot shadow views first), then addOutputPasses. Compile the graph afterwards (FrameGraph::create, or GraphBuilder::plan
///        without a device).
auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void;
}
