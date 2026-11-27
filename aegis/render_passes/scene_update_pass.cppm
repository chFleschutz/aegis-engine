export module aegis.render_passes:scene_update_pass;
import std;
import aegis.math;
import aegis.rhi;
import :frame_scene;
import :gpu_types;
import aegis.render_graph;

export namespace aegis::render_graph
{
/// @brief The frames the projection jitter takes to repeat (detail::haltonJitter).
inline constexpr std::uint32_t jitterSequenceLength = 8;

/// @brief Writes per-frame scene data the other passes read: "CameraData" (a per-frame buffer of
///        CameraData), for the camera of one view (FrameScene::views). It keeps the view's unjittered view-projection
///        for the next frame (CameraData::previousViewProjection) and, with Settings::jitter on, moves the view's
///        rasterization by a sub-pixel offset each frame (CameraData::jitteredViewProjection).
class SceneUpdatePass : public RenderPass
{
public:
    /// @brief Tuning knobs of the view's camera data, not scene data: registered with aegis.reflect
    ///        (passSettingsTypes), so the editor's renderer panel shows them. The pass reads them from its caller's
    ///        object (RenderSettings::camera).
    struct Settings
    {
        /// @brief Jitter the rasterization by a sub-pixel Halton offset each frame, for temporal anti-aliasing; off:
        ///        the jittered matrices are the unjittered ones.
        bool jitter{ false };
    };

    /// @param view The index of the view in FrameScene::views whose camera it writes.
    /// @param settings Read while recording, never copied; it must outlive the pass. None: the view never jitters
    ///        (a shadow view).
    SceneUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view = 0,
        Settings* settings = nullptr);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    [[nodiscard]] auto settings() -> std::optional<PassSettings> override;

private:
    const FrameSceneSource& m_scene;
    std::uint32_t m_view;
    Settings* m_settings;
    ResourceId m_cameraData;
    /// @brief The unjittered view-projection the view was rendered with last frame; none before its first frame and
    ///        after a frame without a camera.
    std::optional<math::Float4x4> m_previousViewProjection;
    std::uint32_t m_frame{ 0 }; // Frames written, which index the jitter sequence
};
}

export namespace aegis::render_graph::detail
{
/// @brief The sub-pixel offset of frame 'frame' of the jitter sequence, in pixels within [-0.5, 0.5]: the Halton
///        sequence of bases 2 (x) and 3 (y), from its second point on (the first is 0), repeating every
///        jitterSequenceLength frames.
[[nodiscard]] auto haltonJitter(std::uint32_t frame) -> math::Float2;

/// @brief 'pixels' as the offset in normalized device coordinates (CameraData::jitter) of a view 'extent' in size.
[[nodiscard]] auto jitterOffset(math::Float2 pixels, rhi::Extent2D extent) -> math::Float2;

/// @brief 'projection' followed by a shift of 'offset' in normalized device coordinates: whatever it projects
///        lands 'offset' further along x and y once divided by w, for a perspective and an orthographic projection.
[[nodiscard]] auto jitterProjection(const math::Float4x4& projection, math::Float2 offset) -> math::Float4x4;

/// @brief The CameraData the shaders read for the frame's camera, jittered by 'jitter' (CameraData::jitter) and
///        with 'previousViewProjection' as last frame's view-projection; none (a view's first frame, a camera cut):
///        this frame's.
[[nodiscard]] auto makeCameraData(const FrameScene::Camera& camera, math::Float2 jitter = {},
    const std::optional<math::Float4x4>& previousViewProjection = std::nullopt) -> CameraData;
}
