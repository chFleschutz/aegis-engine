export module aegis.render_passes:post_effects;
import std;
import :draw_order;
import :frame_scene;
import :gpu_types;
import :pipeline_library;
import :render_system;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Extracts the bright parts of "SceneColor" and blurs them through a mip chain into
///        "Bloom" (mip 0 holds the result), which PostProcessingPass adds back. The threshold is the frame's
///        (FrameScene::PostProcess).
class BloomPass : public RenderPass
{
public:
    static constexpr std::uint32_t mipLevels{ 6 };

    /// @brief Tuning knobs of the blur, not scene data: registered with aegis.reflect (passSettingsTypes), so the
    ///        editor's renderer panel shows them. The pass reads them from its caller's object (RenderSettings::bloom).
    struct Settings
    {
        float downsampleScale{ 1.0f };
        float upsampleScale{ 1.0f };
    };

    /// @param settings Read while recording, never copied; it must outlive the pass.
    BloomPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        Settings& settings);
    ~BloomPass() override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    [[nodiscard]] auto resized(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    [[nodiscard]] auto settings() -> std::optional<PassSettings> override;

private:
    [[nodiscard]] auto createMipViews(ResourcePool& pool) -> std::expected<void, Error>;
    auto freeMipViews() -> void;

    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelines;
    Settings& m_settings;
    ResourceId m_sceneColor;
    ResourceId m_bloom;
    rhi::Device* m_device{ nullptr };
    std::vector<rhi::ImageViewHandle> m_mipViews; // One per mip, storage and sampled
    std::optional<rhi::SamplerHandle> m_sampler;
    const rhi::Pipeline* m_threshold{ nullptr };
    const rhi::Pipeline* m_downsample{ nullptr };
    const rhi::Pipeline* m_upsample{ nullptr };
};

/// @brief Renders into "SceneColor" after lighting, depth tested against "Depth" without writing it:
///        whatever its render systems draw (e.g. PointLightRenderSystem).
class TransparentPass : public RenderPass
{
public:
    /// @param pipelines Where its render systems take their pipelines from.
    /// @param view The index of the view in FrameScene::views it draws (its camera), whose view matrix its render
    ///        systems sort their blended draws by.
    TransparentPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        std::uint32_t view = 0);

    template<typename T, typename... Args>
        requires std::derived_from<T, RenderSystem> && std::constructible_from<T, Args...>
    auto addRenderSystem(Args&&... args) -> T&
    {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *system;
        m_renderSystems.push_back(std::move(system));
        return ref;
    }

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelines;
    std::uint32_t m_view;
    ResourceId m_sceneColor;
    ResourceId m_depth;
    ResourceId m_cameraData;
    std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;
};

/// @brief Draws each of the frame's point lights as a glowing, alpha-blended billboard, back to front in the view of
///        its pass.
class PointLightRenderSystem : public RenderSystem
{
public:
    [[nodiscard]] auto createResources(PipelineLibrary& pipelines, rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> override;
    auto render(const RenderContext& context) -> void override;

private:
    const rhi::Pipeline* m_pipeline{ nullptr };
    std::vector<detail::DepthSortedDraw> m_order; // This frame's billboards, back to front: indices into pointLights
};
}

export namespace aegis::render_graph::detail
{
/// @brief The billboard of one point light: its size grows with intensity and scale.
[[nodiscard]] auto makePointLightConstants(const FrameScene::PointLight& light, rhi::DeviceAddress camera)
    -> PointLightConstants;
}
