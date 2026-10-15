module;
#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <print>
#include <span>
#include <utility>

module aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
namespace
{
struct TextureDescriptor
{
    std::uint32_t image;
    std::uint32_t sampler;
};

auto descriptorOf(const assets::Texture& texture) -> TextureDescriptor
{
    return { texture.sampledDescriptor().index, texture.samplerDescriptor().index };
}

// lighting.slang's push constants.
struct PushConstants
{
    rhi::DeviceAddress lighting;
    std::uint32_t sceneColor;
    std::array<std::uint32_t, 5> gBuffer; // Position, Normal, Albedo, ARM, Emissive
    TextureDescriptor irradiance;
    TextureDescriptor prefiltered;
    TextureDescriptor brdfLut;
};
static_assert(sizeof(PushConstants) <= rhi::CommandBuffer::maxPushConstantSize);

auto storageIndex(const ResourcePool& pool, ResourceHandle handle) -> std::uint32_t
{
    return pool.device().get(pool.texture(handle).view).storageDescriptor()->index;
}
}

LightingPass::LightingPass(ResourcePool& pool) :
    m_gBuffer{
        pool.addReference("Position", Usage::StorageRead),
        pool.addReference("Normal", Usage::StorageRead),
        pool.addReference("Albedo", Usage::StorageRead),
        pool.addReference("ARM", Usage::StorageRead),
        pool.addReference("Emissive", Usage::StorageRead),
    },
    m_sceneColor{ pool.addTexture("SceneColor", Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT }) },
    m_uniforms{ pool.addBuffer("LightingUniforms", Usage::HostWrite, { .size = sizeof(Uniforms), .perFrame = true }) }
{
}

auto LightingPass::info() const -> Info
{
    Info info{ .name = "Lighting", .writes = { m_sceneColor, m_uniforms } };
    info.reads.assign(m_gBuffer.begin(), m_gBuffer.end());
    return info;
}

auto LightingPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto pipeline = createComputePipeline(pool.device(), "Lighting", "lighting.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto LightingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    auto& scene = frameInfo.scene;
    auto& registry = scene.registry();

    const auto environmentEntity = scene.environment();
    if (!environmentEntity || !registry.has<assets::Environment>(environmentEntity))
    {
        static bool reported{ false };
        if (!std::exchange(reported, true))
            std::println("Lighting: the scene has no assets::Environment; skipping lighting");
        return;
    }
    const auto& environment = registry.get<assets::Environment>(environmentEntity);

    Uniforms uniforms{
        .ambientOcclusionFactor = m_ambientOcclusionFactor,
        .viewMode = m_viewMode,
    };
    if (const auto camera = scene.mainCamera(); camera && registry.has<Aegis::GlobalTransform>(camera))
        uniforms.cameraPosition = glm::vec4{ registry.get<Aegis::GlobalTransform>(camera).location, 1.0f };
    if (const auto ambient = scene.ambientLight(); ambient && registry.has<Aegis::AmbientLight>(ambient))
    {
        const auto& light = registry.get<Aegis::AmbientLight>(ambient);
        uniforms.ambientColor = glm::vec4{ light.color, light.intensity };
    }
    if (const auto directional = scene.directionalLight();
        directional && registry.has<Aegis::DirectionalLight, Aegis::GlobalTransform>(directional))
    {
        const auto& light = registry.get<Aegis::DirectionalLight>(directional);
        uniforms.directionalColor = glm::vec4{ light.color, light.intensity };
        uniforms.directionalDirection =
            glm::vec4{ glm::normalize(registry.get<Aegis::GlobalTransform>(directional).forward()), 0.0f };
    }
    for (auto&& [entity, transform, light] : registry.view<Aegis::GlobalTransform, Aegis::PointLight>().each())
    {
        if (uniforms.pointLightCount == static_cast<std::int32_t>(maxPointLights))
            break;
        uniforms.pointLights[static_cast<std::size_t>(uniforms.pointLightCount++)] = {
            .position = glm::vec4{ transform.location, 1.0f },
            .color = glm::vec4{ light.color, light.intensity },
        };
    }
    pool.write(m_uniforms, frameInfo.frameIndex, std::as_bytes(std::span{ &uniforms, 1 }));

    const PushConstants constants{
        .lighting = pool.bufferAddress(m_uniforms, frameInfo.frameIndex),
        .sceneColor = storageIndex(pool, m_sceneColor),
        .gBuffer = {
            storageIndex(pool, m_gBuffer[0]),
            storageIndex(pool, m_gBuffer[1]),
            storageIndex(pool, m_gBuffer[2]),
            storageIndex(pool, m_gBuffer[3]),
            storageIndex(pool, m_gBuffer[4]),
        },
        .irradiance = descriptorOf(*environment.irradiance),
        .prefiltered = descriptorOf(*environment.prefiltered),
        .brdfLut = descriptorOf(*environment.brdfLUT),
    };

    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(constants);
    cmd.dispatch(groupCount(frameInfo.extent.x, 16), groupCount(frameInfo.extent.y, 16));
}

auto LightingPass::drawUI() -> void
{
    ImGui::DragFloat("AO Factor", &m_ambientOcclusionFactor, 0.01f, 0.0f, 1.0f);
    static constexpr std::array viewModeNames{ "Scene Color", "Albedo", "Ambient Occlusion", "Roughness", "Metallic",
        "Emissive" };
    int mode = static_cast<int>(m_viewMode);
    if (ImGui::Combo("View Mode", &mode, viewModeNames.data(), static_cast<int>(viewModeNames.size())))
        m_viewMode = static_cast<LightingViewMode>(mode);
}
}
