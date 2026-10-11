module;
#include <array>
#include <cstdint>
#include <expected>
#include <optional>

export module aegis.render_graph:lighting_pass;
import :error;
import :render_pass;
import :resource;
import :resource_pool;
import aegis.rhi;
import Aegis.Math;

export namespace aegis::render_graph
{
enum class LightingViewMode : std::int32_t
{
    SceneColor = 0,
    Albedo = 1,
    AmbientOcclusion = 2,
    Roughness = 3,
    Metallic = 4,
    Emissive = 5,
};

/// @brief Deferred PBR lighting: G-buffer + the scene's lights and environment (assets::Environment
///        on Scene::environment()) -> "SceneColor" (RGBA16F).
class LightingPass : public RenderPass
{
public:
    static constexpr std::uint32_t maxPointLights{ 128 };

    /// @brief rhi/lighting.slang's Lighting block.
    struct Uniforms
    {
        struct PointLight
        {
            glm::vec4 position;
            glm::vec4 color; // rgb, intensity
        };

        glm::vec4 cameraPosition{ 0.0f };
        glm::vec4 ambientColor{ 0.0f }; // rgb, intensity
        glm::vec4 directionalDirection{ 0.0f, 0.0f, 1.0f, 0.0f };
        glm::vec4 directionalColor{ 0.0f }; // rgb, intensity
        std::array<PointLight, maxPointLights> pointLights{};
        std::int32_t pointLightCount{ 0 };
        float ambientOcclusionFactor{ 1.0f };
        LightingViewMode viewMode{ LightingViewMode::SceneColor };
    };

    explicit LightingPass(ResourcePool& pool);

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;
    auto drawUI() -> void override;

private:
    std::array<ResourceHandle, 5> m_gBuffer; // Position, Normal, Albedo, ARM, Emissive
    ResourceHandle m_sceneColor;
    ResourceHandle m_uniforms;
    std::optional<rhi::Pipeline> m_pipeline;
    LightingViewMode m_viewMode{ LightingViewMode::SceneColor };
    float m_ambientOcclusionFactor{ 1.0f };
};
}
