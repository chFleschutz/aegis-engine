export module aegis.render_passes:shadows;
import std;
import :frame_scene;
import aegis.math;
import aegis.rhi;

// The directional light's shadow: each main view samples shadow views of its own, one per cascade, fitted to its camera.
// The spot lights' shadows: the frame's closest shadow-casting spot lights each get a shadow view, shared by all main
// views, drawn into a tile of one atlas texture ("SpotShadowAtlas").

export namespace aegis::render_graph
{
/// @brief The extent of a shadow view, and so of its shadow map ("Depth").
inline constexpr rhi::Extent2D shadowMapExtent{ 2048, 2048 };
/// @brief How far beyond the shadow's fitted sphere a directional light's shadow view reaches towards the light, in
///        meters: casters outside the camera's slice, such as a roof above it, still cast into the slice.
inline constexpr float shadowCasterReach{ 200.0f };
/// @brief The shadow views (cascades) each main view has, whatever the light's cascade count: the ones past it have
///        no camera, and their passes draw nothing.
inline constexpr std::uint32_t maxShadowCascades{ 4 };
/// @brief The fraction at the far end of each cascade's slice over which the lighting blends it into the next one
///        (the last into no shadow).
inline constexpr float shadowCascadeBlend{ 0.1f };

/// @brief The spot lights that cast shadows in one frame, at most: there is a spot shadow view for each, whether a light
///        uses it or not (the unused ones have no camera, and their passes draw nothing).
inline constexpr std::uint32_t maxShadowedSpotLights{ 4 };
/// @brief The extent of a spot shadow view, and so of its tile of the atlas.
inline constexpr rhi::Extent2D spotShadowTileExtent{ 1024, 1024 };
/// @brief The extent of "SpotShadowAtlas", the depth texture all spot shadow views draw into, a tile each.
inline constexpr rhi::Extent2D spotShadowAtlasExtent{ 2048, 2048 };
static_assert((spotShadowAtlasExtent.x / spotShadowTileExtent.x) * (spotShadowAtlasExtent.y / spotShadowTileExtent.y) >= maxShadowedSpotLights,
    "The spot shadow atlas must hold a tile per shadowed spot light");
/// @brief The widest field of view a spot shadow view has, in radians: a wider cone is shadowed only within it.
inline constexpr float maxSpotShadowFov{ 160.0f * std::numbers::pi_v<float> / 180.0f };

/// @brief The name of the shadow view of cascade 'cascade' of the main view 'view' (and its frame graph scope):
///        "<view>Shadow<cascade>".
[[nodiscard]] auto shadowViewName(std::string_view view, std::uint32_t cascade) -> std::string;

/// @brief The name of the spot shadow view 'index' (and its frame graph scope): "SpotShadow<index>".
[[nodiscard]] auto spotShadowViewName(std::uint32_t index) -> std::string;

/// @brief The tile of "SpotShadowAtlas" the spot shadow view 'index' draws into, in texels: row by row.
[[nodiscard]] auto spotShadowTile(std::uint32_t index) -> rhi::Rect2D;

/// @brief The distances along the camera's axis where each of the light's cascades ends (light.shadowCascades of them,
///        clamped to 1..maxShadowCascades), for a camera with 'projection': from its near plane to light.shadowDistance
///        (as far as its far plane), a mix of logarithmic and uniform splits by light.shadowSplitLambda (1: logarithmic,
///        each cascade the same factor longer than the one before; 0: uniform, all equally long). The last one is the
///        shadow distance.
[[nodiscard]] auto shadowCascadeSplits(const FrameScene::DirectionalLight& light, const math::Float4x4& projection)
    -> std::vector<float>;

/// @brief The camera of a directional light's shadow view for the slice of 'camera' between 'near' and 'far' meters
///        along its axis (a cascade's, shadowCascadeSplits), with a shadow map of 'extent': an orthographic projection
///        looking along the light that covers the bounding sphere of that slice of the camera's frustum, and reaches
///        shadowCasterReach further towards the light. The sphere follows only the camera's projection, so turning the
///        camera keeps its size, and its center moves in whole texels of the shadow map: the shadow's edges don't
///        shimmer as the camera turns or moves.
[[nodiscard]] auto fitDirectionalShadow(const FrameScene::DirectionalLight& light, const FrameScene::Camera& camera,
    float near, float far, rhi::Extent2D extent = shadowMapExtent) -> FrameScene::Camera;

/// @brief The indices in scene.spotLights of the spot lights that cast shadows this frame: those with castsShadows, the
///        closest to 'from' first (ties in the scene's order), at most 'budget' of them.
[[nodiscard]] auto selectShadowedSpotLights(const FrameScene& scene, const math::Float3& from,
    std::uint32_t budget = maxShadowedSpotLights) -> std::vector<std::uint32_t>;

/// @brief The camera of a spot light's shadow view, for a square tile: a perspective projection from the light along
///        its direction, covering its outer cone (at most maxSpotShadowFov), from max(range / 1000, 5 cm) to its range.
[[nodiscard]] auto fitSpotShadow(const FrameScene::SpotLight& light) -> FrameScene::Camera;

/// @brief Appends maxShadowCascades shadow views (ViewKind::Shadow, shadowViewName, shadowMapExtent) for each main view
///        of 'scene' that has none, and points the main view at the first (FrameScene::View::shadow). Each cascade's
///        camera is the directional light's, fitted to its slice of the main view's camera (shadowCascadeSplits,
///        fitDirectionalShadow); none without either or past the light's cascade count, and its passes then draw
///        nothing. Then, once, maxShadowedSpotLights spot shadow views (ViewKind::Shadow, spotShadowViewName,
///        spotShadowTileExtent, spotShadowTile) shared by all main views: one for each spot light
///        selectShadowedSpotLights picks by distance to the first main view's camera, with fitSpotShadow's camera, which
///        it points at its view (FrameScene::SpotLight::shadowView); the others have no camera. The views come in the
///        order addShadowViews gives the graph's (DefaultPassesDesc::views).
auto addShadowViews(FrameScene& scene) -> void;
}
