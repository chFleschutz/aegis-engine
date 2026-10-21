export module aegis.render_graph:lighting_pass;
import std;
import :error;
import :frame_scene;
import :gpu_types;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
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

    explicit LightingPass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

private:
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
/// @brief The lighting block for the frame's camera, ambient and directional light and point lights. A
///        missing piece keeps its default (no ambient, a dark directional light along +Z, the camera at the
///        origin); point lights beyond maxPointLights are dropped.
[[nodiscard]] auto packLighting(const FrameScene& scene, float ambientOcclusionFactor, LightingViewMode viewMode)
    -> LightingUniforms;
}
