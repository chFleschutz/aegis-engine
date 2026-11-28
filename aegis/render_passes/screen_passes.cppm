export module aegis.render_passes:screen_passes;
import std;
import :frame_scene;
import :gpu_types;
import :names;
import :pipeline_library;
import aegis.render_graph;
import aegis.math;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws the scene environment's sky box into "SceneColor" wherever "Depth" is still clear.
class SkyBoxPass : public RenderPass
{
public:
    /// @param view The index of the view in FrameScene::views whose camera it draws the sky for.
    SkyBoxPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        std::uint32_t view = 0);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelines;
    std::uint32_t m_view;
    ResourceId m_sceneColor;
    ResourceId m_depth;
    const rhi::Pipeline* m_pipeline{ nullptr };
};

/// @brief Tone maps "TemporalColor" (plus "Bloom", if enabled) into "Final" (RGBA16F), gamma encoded
///        unless the frame's target is an sRGB format that encodes by itself. The settings are the frame's
///        (FrameScene::PostProcess).
class PostProcessingPass : public RenderPass
{
public:
    struct Desc
    {
        /// @brief The frame's target encodes on write (an sRGB format, DefaultPassesDesc::targetEncodes): "Final"
        ///        stays linear, as gamma here would apply it twice.
        bool targetEncodes{ false };
        /// @brief Read "Bloom" (a BloomPass must be in the graph); otherwise bloom is off.
        bool bloom{ true };
    };

    PostProcessingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        Desc desc);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelines;
    ResourceId m_color; // "TemporalColor"
    std::optional<ResourceId> m_bloom;
    ResourceId m_final;
    bool m_targetEncodes;
    const rhi::Pipeline* m_pipeline{ nullptr };
};

/// @brief Copies a finished image (by default "Final") onto the frame's target (names::target): the swapchain image
///        or the renderer's offscreen target.
class PresentPass : public RenderPass
{
public:
    /// @param targetFormat The format of the frame's target (renderer::Renderer::targetFormat()).
    PresentPass(ResourceDeclarations& declarations, rhi::Format targetFormat, std::string_view source = names::finalImage);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceId m_source;
    ResourceId m_target;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The sky box's view-projection (row-major): the camera's rotation only, as the sky is infinitely
///        far away.
[[nodiscard]] auto makeSkyBoxViewProjection(const FrameScene::Camera& camera) -> math::Float4x4;
}
