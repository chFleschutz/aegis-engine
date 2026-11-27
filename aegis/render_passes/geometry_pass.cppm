export module aegis.render_passes:geometry_pass;
import std;
import :frame_scene;
import :names;
import :pipeline_cache;
import :pipeline_library;
import :render_system;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Where a depth-only geometry pass draws instead of a "Depth" of its own: a tile of a depth texture another pass
///        creates, such as a spot shadow view's tile of "SpotShadowAtlas". The pass clears and draws only the tile.
struct DepthTile
{
    std::string texture; // Its name, found as addReference finds it
    rhi::Rect2D rect;    // In texels
};

/// @brief Creates "SpotShadowAtlas" (spotShadowAtlasExtent, GeometryPass::depthFormat) and clears it, before the spot
///        shadow views draw their tiles into it (DepthTile).
class SpotShadowAtlasPass : public RenderPass
{
public:
    explicit SpotShadowAtlasPass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceId m_atlas;
};

/// @brief The CPU-driven geometry pass: clears and fills "Position", "Normal", "Albedo", "ARM", "Emissive",
///        "MotionVectors" and "Depth" through its render systems, or only "Depth" with Technique::Depth (a shadow
///        view's), or only a tile of another pass's depth texture (DepthTile). Its render systems draw with the same
///        technique (StaticMeshRenderSystem's). Without a camera for its view it clears its targets and draws nothing.
///
///        For the G-buffer it also writes every instance's previous model (Instance::previousModel) into
///        "PreviousModels", a per-frame buffer of math::Float3x4 holding the static instances, then the dynamic ones,
///        and passes its address on (RenderContext::previousModels): the push constants have no room for a second
///        model matrix.
class GeometryPass : public RenderPass
{
public:
    static constexpr std::array gBufferFormats{
        rhi::Format::RGBA16_SFLOAT, // Position
        rhi::Format::RGBA16_SFLOAT, // Normal
        rhi::Format::RGBA8_UNORM,   // Albedo
        rhi::Format::RGBA8_UNORM,   // ARM: ambient occlusion, roughness, metallic
        rhi::Format::RGBA8_UNORM,   // Emissive
        rhi::Format::RG16_SFLOAT,   // Motion vectors: screen uv moved since the last frame
    };
    /// @brief Instances "PreviousModels" holds before it first grows.
    static constexpr std::uint32_t initialPreviousModelCapacity{ 4096 };
    static constexpr auto depthFormat = rhi::Format::D32_SFLOAT;

    /// @param view The index of the view in FrameScene::views it draws (whose "CameraData" it reads).
    /// @param tile Where a Technique::Depth pass draws instead of its own "Depth"; none: its own.
    /// @param pipelines Where its render systems take their pipelines from.
    GeometryPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        Technique technique = Technique::GBuffer, std::uint32_t view = 0, std::optional<DepthTile> tile = std::nullopt);

    /// @brief Declares the G-buffer textures, in the order of gBufferFormats. The GPU-driven geometry pass fills the
    ///        same targets, so both declare them here.
    static auto declareGBuffer(ResourceDeclarations& declarations) -> std::array<ResourceId, gBufferFormats.size()>;
    /// @brief Declares the depth texture, in depthFormat; with a tile, the access to the texture it is a tile of.
    static auto declareDepth(ResourceDeclarations& declarations, const std::optional<DepthTile>& tile = std::nullopt) -> ResourceId;
    /// @brief The rendering area of a depth target: the tile, or all of 'extent'.
    [[nodiscard]] static auto depthArea(const std::optional<DepthTile>& tile, rhi::Extent2D extent) -> rhi::Rect2D;

    template<typename T, typename... Args>
        requires std::derived_from<T, RenderSystem> && std::constructible_from<T, Args...>
    auto addRenderSystem(Args&&... args) -> T&
    {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *system;
        m_renderSystems.push_back(std::move(system));
        return ref;
    }

    [[nodiscard]] auto technique() const noexcept -> Technique { return m_technique; }
    [[nodiscard]] auto renderSystems() const noexcept -> std::span<const std::unique_ptr<RenderSystem>> { return m_renderSystems; }

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelines;
    Technique m_technique;
    std::uint32_t m_view;
    std::vector<ResourceId> m_gBuffer; // Empty with Technique::Depth
    ResourceId m_depth;
    std::optional<DepthTile> m_tile;
    ResourceId m_cameraData;
    std::optional<ResourceId> m_previousModels; // The G-buffer technique's
    std::uint32_t m_previousModelCapacity{ initialPreviousModelCapacity };
    std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;
};
}
