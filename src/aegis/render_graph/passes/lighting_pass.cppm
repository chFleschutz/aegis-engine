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
/// @brief Writes the frame's point and spot lights into "Lights" (a per-frame buffer of LightData, point lights
///        first), which the lighting passes of all views read. The buffer grows with the light count
///        (detail::grownCapacity); should it fail to, only the lights that fit are written, and lit.
class LightUpdatePass : public RenderPass
{
public:
    /// @brief Lights the buffer holds before it first grows.
    static constexpr std::uint32_t initialCapacity{ 256 };

    LightUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    ResourceId m_lights;
    std::uint32_t m_capacity{ initialCapacity };
};

/// @brief Deferred PBR lighting: G-buffer + the frame's lights ("Lights", LightUpdatePass) and environment and the
///        BRDF lookup table, which it owns -> "SceneColor" (RGBA16F).
class LightingPass : public RenderPass
{
public:
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
    ResourceId m_lights;
    std::optional<rhi::Pipeline> m_pipeline;
    std::shared_ptr<assets::Texture> m_brdfLut; // One per pass, not per environment
    LightingViewMode m_viewMode{ LightingViewMode::SceneColor };
    float m_ambientOcclusionFactor{ 1.0f };
};
}

export namespace aegis::render_graph::detail
{
/// @brief The light records of the scene's point and spot lights, in the order the light buffer holds them: the point
///        lights, then the spot lights, each in the scene's order.
[[nodiscard]] auto packLights(const FrameScene& scene) -> std::vector<LightData>;

/// @brief The light record of a point light: a cone that lets everything through.
[[nodiscard]] auto packLight(const FrameScene::PointLight& light) -> LightData;
/// @brief The light record of a spot light. An outer cone narrower than the inner one is treated as as wide as it
///        (a hard edge).
[[nodiscard]] auto packLight(const FrameScene::SpotLight& light) -> LightData;

/// @brief The lighting block for the camera of the view 'view', the ambient and directional light, and the lights
///        in the buffer at 'lights' (packLights' records, of which 'lightCapacity' fit). A missing piece keeps its
///        default (no ambient, a dark directional light along +Z, the camera at the origin); lights that don't fit
///        are left out, spot lights first.
[[nodiscard]] auto packLighting(const FrameScene& scene, std::uint32_t view, rhi::DeviceAddress lights,
    std::uint32_t lightCapacity, float ambientOcclusionFactor, LightingViewMode viewMode) -> LightingUniforms;
}
