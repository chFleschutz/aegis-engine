export module aegis.render_passes:shadows;
import std;
import :frame_scene;
import aegis.math;
import aegis.rhi;

// The directional light's shadow: each main view samples a shadow view of its own, fitted to its camera.

export namespace aegis::render_graph
{
/// @brief The extent of a shadow view, and so of its shadow map ("Depth").
inline constexpr rhi::Extent2D shadowMapExtent{ 2048, 2048 };
/// @brief How far beyond the shadow's fitted sphere a directional light's shadow view reaches towards the light, in
///        meters: casters outside the camera's slice, such as a roof above it, still cast into the slice.
inline constexpr float shadowCasterReach{ 200.0f };

/// @brief The name of the shadow view of the main view 'view' (and its frame graph scope): "<view>Shadow".
[[nodiscard]] auto shadowViewName(std::string_view view) -> std::string;

/// @brief The camera of a directional light's shadow view for 'camera', with a shadow map of 'extent': an orthographic
///        projection looking along the light that covers the bounding sphere of the camera's frustum from its near
///        plane to light.shadowDistance, and reaches shadowCasterReach further towards the light. The sphere follows
///        only the camera's projection, so turning the camera keeps its size, and its center moves in whole texels
///        of the shadow map: the shadow's edges don't shimmer as the camera turns or moves.
[[nodiscard]] auto fitDirectionalShadow(const FrameScene::DirectionalLight& light, const FrameScene::Camera& camera,
    rhi::Extent2D extent = shadowMapExtent) -> FrameScene::Camera;

/// @brief Appends a shadow view (ViewKind::Shadow, shadowViewName, shadowMapExtent) for each main view of 'scene'
///        that has none, and points the main view at it (FrameScene::View::shadow). Its camera is the directional
///        light's, fitted to the main view's camera (fitDirectionalShadow); none without either, and its passes then
///        draw nothing. The views come in the order addShadowViews gives the graph's (DefaultPassesDesc::views).
auto addShadowViews(FrameScene& scene) -> void;
}
