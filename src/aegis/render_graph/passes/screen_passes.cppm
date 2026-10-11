module;
#include <expected>
#include <optional>

export module aegis.render_graph:screen_passes;
import :error;
import :render_pass;
import :resource;
import :resource_pool;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws the scene environment's sky box into "SceneColor" wherever "Depth" is still clear.
class SkyBoxPass : public RenderPass
{
public:
    explicit SkyBoxPass(ResourcePool& pool);

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceHandle m_sceneColor;
    ResourceHandle m_depth;
    std::optional<rhi::Pipeline> m_pipeline;
};

/// @brief Tone maps "SceneColor" (plus "Bloom", if enabled) into "Final" (RGBA16F), gamma encoded
///        unless the frame's target is an sRGB format that encodes by itself.
class PostProcessingPass : public RenderPass
{
public:
    enum class ToneMapping : int
    {
        Reinhard,
        Aces,
    };

    struct Settings
    {
        ToneMapping toneMapping{ ToneMapping::Aces };
        float bloomIntensity{ 0.1f };
        float exposure{ 1.0f };
        float gamma{ 2.2f };
    };

    /// @param bloom Read "Bloom" (a BloomPass must be in the graph); otherwise bloom is off.
    PostProcessingPass(ResourcePool& pool, bool bloom = true);

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

    [[nodiscard]] auto settings() noexcept -> Settings& { return m_settings; }

private:
    ResourceHandle m_sceneColor;
    std::optional<ResourceHandle> m_bloom;
    ResourceHandle m_final;
    Settings m_settings;
    std::optional<rhi::Pipeline> m_pipeline;
};

/// @brief Copies a finished image (by default "Final") onto the frame's target: the swapchain image
///        or the renderer's offscreen target.
class PresentPass : public RenderPass
{
public:
    explicit PresentPass(ResourcePool& pool, const char* source = "Final");

    [[nodiscard]] auto info() const -> Info override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceHandle m_source;
};
}
