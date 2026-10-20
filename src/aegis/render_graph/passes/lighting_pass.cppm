export module aegis.render_graph:lighting_pass;
import std;
import :error;
import :gpu_types;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
import aegis.rhi;
import aegis.math;
import aegis.scene;

export namespace aegis::render_graph
{
/// @brief Deferred PBR lighting: G-buffer + the scene's lights and environment (assets::Environment
///        on the first entity that has one) -> "SceneColor" (RGBA16F).
class LightingPass : public RenderPass
{
public:
    static constexpr std::uint32_t maxPointLights{ render_graph::maxPointLights };

    explicit LightingPass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

private:
    std::array<ResourceHandle, 5> m_gBuffer; // Position, Normal, Albedo, ARM, Emissive
    ResourceHandle m_sceneColor;
    ResourceHandle m_uniforms;
    std::optional<rhi::Pipeline> m_pipeline;
    LightingViewMode m_viewMode{ LightingViewMode::SceneColor };
    float m_ambientOcclusionFactor{ 1.0f };
};
}

export namespace aegis::render_graph::detail
{
/// @brief The lighting block for the scene's main camera, ambient and directional light and its point
///        lights. A missing piece keeps its default (no ambient, a dark directional light along +Z, the
///        camera at the origin); point lights beyond maxPointLights are dropped.
[[nodiscard]] auto gatherLighting(scene::Scene& scene, float ambientOcclusionFactor, LightingViewMode viewMode)
    -> LightingUniforms;
}
