export module aegis.render_passes:lighting_pass;
import std;
import :frame_scene;
import :gpu_types;
import :shadows;
import aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Writes the frame's point and spot lights into "Lights" (a per-frame buffer of LightData, point lights
///        first), which the lighting passes of all views read. The buffer grows with the light count in prepare
///        (detail::growBuffer).
class LightUpdatePass : public RenderPass
{
public:
    /// @brief Lights the buffer holds before it first grows.
    static constexpr std::uint32_t initialCapacity{ 256 };

    LightUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene);

    [[nodiscard]] auto name() const -> std::string_view override;
    /// @brief Grows the light buffer to the frame's lights.
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    ResourceId m_lights;
    std::uint32_t m_capacity{ initialCapacity };
};

/// @brief Deferred PBR lighting: G-buffer + the frame's lights ("Lights", LightUpdatePass) and environment, the
///        directional light's shadow maps (its cascades), the spot lights' shadow atlas and the BRDF lookup table, which
///        it owns -> "SceneColor" (RGBA16F).
class LightingPass : public RenderPass
{
public:
    /// @brief The shadow views whose "Depth" shadow the directional light, one per cascade, and the atlas of the spot
    ///        lights' shadow views, each sampled with 3x3 PCF.
    struct ShadowMaps
    {
        std::uint32_t view; // The index in FrameScene::views of the first; the others follow it (their cameras)
        /// @brief Each cascade's "Depth" by scoped name, "<view>Shadow<cascade>/Depth" (scopedName).
        std::array<std::string, maxShadowCascades> maps;
        /// @brief "SpotShadowAtlas", whose tiles the light records name; empty: the spot lights are unshadowed.
        std::string spotAtlas;
    };

    /// @param view The index of the view in FrameScene::views it lights (its camera position).
    /// @param shadow None: the directional light is unshadowed.
    LightingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view = 0,
        std::optional<ShadowMaps> shadow = std::nullopt);
    LightingPass(const LightingPass&) = delete;
    LightingPass(LightingPass&&) = delete;
    ~LightingPass() override;

    auto operator=(const LightingPass&) -> LightingPass& = delete;
    auto operator=(LightingPass&&) -> LightingPass& = delete;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    /// @brief Warns once when the frame's scene has no environment to light with: execute then records nothing.
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

private:
    const FrameSceneSource& m_scene;
    std::uint32_t m_view;
    std::array<ResourceId, 5> m_gBuffer; // Position, Normal, Albedo, ARM, Emissive
    ResourceId m_sceneColor;
    ResourceId m_uniforms;
    ResourceId m_lights;
    std::optional<std::uint32_t> m_shadowView;
    std::array<ResourceId, maxShadowCascades> m_shadowMaps{}; // Invalid without shadow views
    ResourceId m_spotShadowAtlas;                             // Invalid without spot shadow views
    rhi::Device* m_device{ nullptr };
    std::optional<rhi::SamplerHandle> m_shadowSampler;
    std::optional<rhi::Pipeline> m_pipeline;
    std::shared_ptr<assets::Texture> m_brdfLut; // One per pass, not per environment
    LightingViewMode m_viewMode{ LightingViewMode::SceneColor };
    float m_ambientOcclusionFactor{ 1.0f };
};
}

export namespace aegis::render_graph::detail
{
/// @brief The light records of the scene's point and spot lights, in the order the light buffer holds them: the point
///        lights, then the spot lights, each in the scene's order. A spot light with a spot shadow view that has a
///        camera and a tile (FrameScene::SpotLight::shadowView, addShadowViews) carries its shadow (packSpotShadow).
[[nodiscard]] auto packLights(const FrameScene& scene) -> std::vector<LightData>;

/// @brief Fills the shadow of a spot light's record: the view-projection of 'camera' (a spot shadow view's, fitSpotShadow),
///        the tile 'tile' of "SpotShadowAtlas" in uv, the light's bias and the texel size of the tile.
auto packSpotShadow(LightData& data, const FrameScene::SpotLight& light, const FrameScene::Camera& camera, rhi::Rect2D tile) -> void;

/// @brief The light record of a point light: a cone that lets everything through.
[[nodiscard]] auto packLight(const FrameScene::PointLight& light) -> LightData;
/// @brief The light record of a spot light. An outer cone narrower than the inner one is treated as as wide as it
///        (a hard edge).
[[nodiscard]] auto packLight(const FrameScene::SpotLight& light) -> LightData;

/// @brief The shadow maps for packLighting: the shadow views they are the "Depth" of, and their descriptors.
struct ShadowMapInput
{
    std::uint32_t view; // The index in FrameScene::views of the first cascade's shadow view; the others follow it
    std::array<rhi::TextureDescriptor, maxShadowCascades> maps{};
    std::optional<rhi::TextureDescriptor> spotAtlas; // None: the spot lights are unshadowed
};

/// @brief The lighting block for the camera of the view 'view', the ambient and directional light, and the lights
///        in the buffer at 'lights' (packLights' records, of which 'lightCapacity' fit). A missing piece keeps its
///        default (no ambient, a dark directional light along +Z, the camera at the origin); lights that don't fit
///        are left out, spot lights first. The directional light is shadowed by the cascades of 'shadow' when there
///        is one and the view has a camera: the light's cascade count of them (shadowCascadeSplits), as far as their
///        views have cameras (orthographic ones, fitDirectionalShadow's). The spot lights are shadowed through
///        shadow->spotAtlas when there is one (their records carry their tiles). The ambient light reflects the scene
///        environment's maps, with 'brdfLut' (Ibl::brdfLut, the lighting pass's).
[[nodiscard]] auto packLighting(const FrameScene& scene, std::uint32_t view, rhi::DeviceAddress lights,
    std::uint32_t lightCapacity, float ambientOcclusionFactor, LightingViewMode viewMode,
    std::optional<ShadowMapInput> shadow = std::nullopt, rhi::TextureDescriptor brdfLut = {}) -> LightingUniforms;
}
