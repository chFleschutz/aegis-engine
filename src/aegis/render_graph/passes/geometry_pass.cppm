export module aegis.render_graph:geometry_pass;
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
/// @brief The CPU-driven G-buffer pass: clears and fills "Position", "Normal", "Albedo", "ARM",
///        "Emissive" and "Depth" through its render systems.
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

    explicit GeometryPass(ResourceDeclarations& declarations);

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
    std::array<ResourceHandle, gBufferFormats.size()> m_gBuffer;
    ResourceHandle m_depth;
    ResourceHandle m_cameraData;
    std::vector<std::unique_ptr<RenderSystem>> m_renderSystems;
};
}
