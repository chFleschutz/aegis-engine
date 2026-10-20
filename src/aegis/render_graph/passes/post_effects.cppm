export module aegis.render_graph:post_effects;
import std;
import :error;
import :render_pass;
import :render_system;
import :resource;
import :declarations;
import :resource_pool;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Extracts the bright parts of "SceneColor" and blurs them through a mip chain into
///        "Bloom" (mip 0 holds the result), which PostProcessingPass adds back.
class BloomPass : public RenderPass
{
public:
    static constexpr std::uint32_t mipLevels{ 6 };

    struct Settings
    {
        float threshold{ 0.0f };
        float softThreshold{ 0.5f };
        float downsampleScale{ 1.0f };
        float upsampleScale{ 1.0f };
    };

    explicit BloomPass(ResourceDeclarations& declarations);
    ~BloomPass() override;

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    [[nodiscard]] auto resized(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

    [[nodiscard]] auto settings() noexcept -> Settings& { return m_settings; }

private:
    [[nodiscard]] auto createMipViews(ResourcePool& pool) -> std::expected<void, Error>;
    auto freeMipViews() -> void;

    ResourceHandle m_sceneColor;
    ResourceHandle m_bloom;
    Settings m_settings;
    rhi::Device* m_device{ nullptr };
    std::vector<rhi::ImageViewHandle> m_mipViews; // One per mip, storage and sampled
    std::optional<rhi::SamplerHandle> m_sampler;
    std::optional<rhi::Pipeline> m_threshold;
    std::optional<rhi::Pipeline> m_downsample;
    std::optional<rhi::Pipeline> m_upsample;
};

/// @brief Renders into "SceneColor" after lighting, depth tested against "Depth" without writing it:
///        whatever its render systems draw (e.g. PointLightRenderSystem).
class TransparentPass : public RenderPass
{
public:
    explicit TransparentPass(ResourceDeclarations& declarations);

    template<typename T, typename... Args>
        requires std::derived_from<T, RenderSystem> && std::constructible_from<T, Args...>
    auto addRenderSystem(Args&&... args) -> T&
    {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *system;
        m_renderSystems.push_back(std::move(system));
        return ref;
    }

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceHandle m_sceneColor;
    ResourceHandle m_depth;
    ResourceHandle m_cameraData;
    std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;
};

/// @brief Draws each aegis::PointLight as a glowing, alpha-blended billboard.
class PointLightRenderSystem : public RenderSystem
{
public:
    [[nodiscard]] auto createResources(rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> override;
    auto render(const RenderContext& context) -> void override;

private:
    std::optional<rhi::Pipeline> m_pipeline;
};
}
