export module aegis.render_passes:lighting_pass;
import std;
import :frame_scene;
import :gpu_types;
import aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Deferred PBR lighting: G-buffer + the frame's lights and environment and the BRDF lookup
///        table, which it owns -> "SceneColor" (RGBA16F).
class LightingPass : public RenderPass
{
public:
    static constexpr std::uint32_t maxPointLights{ render_graph::maxPointLights };

    /// @param view The index of the view in FrameScene::views it lights (its camera position).
    LightingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view = 0);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

private:
    const FrameSceneSource& m_scene;
    std::uint32_t m_view;
    std::array<ResourceId, 5> m_gBuffer; // Position, Normal, Albedo, ARM, Emissive
    ResourceId m_sceneColor;
    ResourceId m_uniforms;
    std::optional<rhi::Pipeline> m_pipeline;
    std::shared_ptr<assets::Texture> m_brdfLut; // One per pass, not per environment
    LightingViewMode m_viewMode{ LightingViewMode::SceneColor };
    float m_ambientOcclusionFactor{ 1.0f };
};
}

export namespace aegis::render_graph::detail
{
/// @brief The lighting block for the camera of the view 'view', the ambient and directional light and the point
///        lights. A missing piece keeps its default (no ambient, a dark directional light along +Z, the camera at the
///        origin); point lights beyond maxPointLights are dropped.
[[nodiscard]] auto packLighting(const FrameScene& scene, std::uint32_t view, float ambientOcclusionFactor,
    LightingViewMode viewMode) -> LightingUniforms;
}
