export module aegis.render_passes:forward_pass;
import std;
import :frame_scene;
import :lighting_pass;
import :pipeline_library;
import :render_system;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws lit surfaces into "SceneColor" after the sky, depth tested against "Depth" without writing it: whatever
///        its render systems draw with the forward technique (StaticMeshRenderSystem's blended draw batches, on both
///        geometry paths). They shade with the view's lighting block, which its LightingPass wrote this frame
///        ("LightingUniforms", RenderContext::lighting), so the pass reads the lights and shadow maps that block names.
///        Like the lighting pass, it draws nothing without a camera for its view or an environment in the scene.
class ForwardPass : public RenderPass
{
public:
    /// @param pipelines Where its render systems take their pipelines from.
    /// @param view The index of the view in FrameScene::views it draws (its camera).
    /// @param shadow The shadow maps the view's LightingPass samples; none: it has none.
    ForwardPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        std::uint32_t view = 0, const std::optional<LightingPass::ShadowMaps>& shadow = std::nullopt);

    template<typename T, typename... Args>
        requires std::derived_from<T, RenderSystem> && std::constructible_from<T, Args...>
    auto addRenderSystem(Args&&... args) -> T&
    {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *system;
        m_renderSystems.push_back(std::move(system));
        return ref;
    }

    [[nodiscard]] auto renderSystems() const noexcept -> std::span<const std::unique_ptr<RenderSystem>> { return m_renderSystems; }

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
    ResourceId m_lighting;
    ResourceId m_lights;
    std::vector<ResourceId> m_shadowMaps; // The cascades' maps and the spot shadow atlas, as the view has them
    std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;
};
}
