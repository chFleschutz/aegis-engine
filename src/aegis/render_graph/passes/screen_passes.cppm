export module aegis.render_graph:screen_passes;
import std;
import :error;
import :frame_scene;
import :gpu_types;
import :names;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
import aegis.math;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws the scene environment's sky box into "SceneColor" wherever "Depth" is still clear.
class SkyBoxPass : public RenderPass
{
public:
    explicit SkyBoxPass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceId m_sceneColor;
    ResourceId m_depth;
    std::optional<rhi::Pipeline> m_pipeline;
};

/// @brief Tone maps "SceneColor" (plus "Bloom", if enabled) into "Final" (RGBA16F), gamma encoded
///        unless the frame's target is an sRGB format that encodes by itself.
class PostProcessingPass : public RenderPass
{
public:
    using ToneMapping = render_graph::ToneMapping;

    struct Settings
    {
        ToneMapping toneMapping{ ToneMapping::Aces };
        float bloomIntensity{ 0.1f };
        float exposure{ 1.0f };
        float gamma{ 2.2f };
    };

    /// @param bloom Read "Bloom" (a BloomPass must be in the graph); otherwise bloom is off.
    PostProcessingPass(ResourceDeclarations& declarations, bool bloom = true);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

    [[nodiscard]] auto settings() noexcept -> Settings& { return m_settings; }

private:
    ResourceId m_sceneColor;
    std::optional<ResourceId> m_bloom;
    ResourceId m_final;
    Settings m_settings;
    std::optional<rhi::Pipeline> m_pipeline;
};

/// @brief Copies a finished image (by default "Final") onto the frame's target: the swapchain image
///        or the renderer's offscreen target.
class PresentPass : public RenderPass
{
public:
    explicit PresentPass(ResourceDeclarations& declarations, std::string_view source = names::finalImage);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceId m_source;
};
}

export namespace aegis::render_graph::detail
{
/// @brief The sky box's view-projection (row-major): the camera's rotation only, as the sky is infinitely
///        far away.
[[nodiscard]] auto makeSkyBoxViewProjection(const FrameScene::Camera& camera) -> glm::mat4;
}
