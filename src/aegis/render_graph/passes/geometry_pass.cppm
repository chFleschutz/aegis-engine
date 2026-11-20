export module aegis.render_passes:geometry_pass;
import std;
import :frame_scene;
import :names;
import :pipeline_cache;
import :render_system;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief The CPU-driven geometry pass: clears and fills "Position", "Normal", "Albedo", "ARM", "Emissive" and
///        "Depth" through its render systems, or only "Depth" with Technique::Depth (a shadow view's). Its render
///        systems draw with the same technique (StaticMeshRenderSystem's).
class GeometryPass : public RenderPass
{
public:
    static constexpr std::array gBufferFormats{
        rhi::Format::RGBA16_SFLOAT, // Position
        rhi::Format::RGBA16_SFLOAT, // Normal
        rhi::Format::RGBA8_UNORM,   // Albedo
        rhi::Format::RGBA8_UNORM,   // ARM: ambient occlusion, roughness, metallic
        rhi::Format::RGBA8_UNORM,   // Emissive
    };
    static constexpr auto depthFormat = rhi::Format::D32_SFLOAT;

    GeometryPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, Technique technique = Technique::GBuffer);

    /// @brief Declares the G-buffer textures, in the order of gBufferFormats. The GPU-driven geometry pass fills the
    ///        same targets, so both declare them here.
    static auto declareGBuffer(ResourceDeclarations& declarations) -> std::array<ResourceId, gBufferFormats.size()>;
    /// @brief Declares the depth texture, in depthFormat.
    static auto declareDepth(ResourceDeclarations& declarations) -> ResourceId;

    template<typename T, typename... Args>
        requires std::derived_from<T, RenderSystem> && std::constructible_from<T, Args...>
    auto addRenderSystem(Args&&... args) -> T&
    {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *system;
        m_renderSystems.push_back(std::move(system));
        return ref;
    }

    [[nodiscard]] auto technique() const noexcept -> Technique { return m_technique; }
    [[nodiscard]] auto renderSystems() const noexcept -> std::span<const std::unique_ptr<RenderSystem>> { return m_renderSystems; }

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    Technique m_technique;
    std::vector<ResourceId> m_gBuffer; // Empty with Technique::Depth
    ResourceId m_depth;
    ResourceId m_cameraData;
    std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;
};
}
