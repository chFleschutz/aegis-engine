module;
#include <imgui.h>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.base;
import aegis.rhi;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
LightingPass::LightingPass(ResourcePool& pool) :
    m_gBuffer{
        pool.addReference("Position", Usage::StorageRead),
        pool.addReference("Normal", Usage::StorageRead),
        pool.addReference("Albedo", Usage::StorageRead),
        pool.addReference("ARM", Usage::StorageRead),
        pool.addReference("Emissive", Usage::StorageRead),
    },
    m_sceneColor{ pool.addTexture("SceneColor", Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT }) },
    m_uniforms{ pool.addBuffer("LightingUniforms", Usage::HostWrite, { .size = sizeof(LightingUniforms), .perFrame = true }) }
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
            log::warn("Lighting: the scene has no assets::Environment; skipping lighting");
        return;
    }
    const auto& environment = registry.get<assets::Environment>(environmentEntity);

    LightingUniforms uniforms{
        .ambientOcclusionFactor = m_ambientOcclusionFactor,
        .viewMode = m_viewMode,
    };
    if (const auto camera = scene.mainCamera(); camera && registry.has<aegis::GlobalTransform>(camera))
        uniforms.cameraPosition = glm::vec4{ registry.get<aegis::GlobalTransform>(camera).location, 1.0f };
    if (const auto ambient = scene.ambientLight(); ambient && registry.has<aegis::AmbientLight>(ambient))
    {
        const auto& light = registry.get<aegis::AmbientLight>(ambient);
        uniforms.ambientColor = glm::vec4{ light.color, light.intensity };
    }
    if (const auto directional = scene.directionalLight();
        directional && registry.has<aegis::DirectionalLight, aegis::GlobalTransform>(directional))
    {
        const auto& light = registry.get<aegis::DirectionalLight>(directional);
        uniforms.directionalColor = glm::vec4{ light.color, light.intensity };
        uniforms.directionalDirection =
            glm::vec4{ glm::normalize(registry.get<aegis::GlobalTransform>(directional).forward()), 0.0f };
    }
    for (auto&& [entity, transform, light] : registry.view<aegis::GlobalTransform, aegis::PointLight>().each())
    {
        if (uniforms.pointLightCount == static_cast<std::int32_t>(maxPointLights))
            break;
        uniforms.pointLights[static_cast<std::size_t>(uniforms.pointLightCount++)] = {
            .position = glm::vec4{ transform.location, 1.0f },
            .color = glm::vec4{ light.color, light.intensity },
        };
    }
    pool.write(m_uniforms, frameInfo.frameIndex, std::as_bytes(std::span{ &uniforms, 1 }));

    const LightingConstants constants{
        .lighting = pool.bufferAddress(m_uniforms, frameInfo.frameIndex),
        .sceneColor = pool.storageDescriptor(m_sceneColor),
        .gBuffer = {
            pool.storageDescriptor(m_gBuffer[0]),
            pool.storageDescriptor(m_gBuffer[1]),
            pool.storageDescriptor(m_gBuffer[2]),
            pool.storageDescriptor(m_gBuffer[3]),
            pool.storageDescriptor(m_gBuffer[4]),
        },
        .irradiance = environment.irradiance->descriptor(),
        .prefiltered = environment.prefiltered->descriptor(),
        .brdfLut = environment.brdfLUT->descriptor(),
    };

    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(constants);
    cmd.dispatch(groupCount(frameInfo.extent.x, computeGroupSize), groupCount(frameInfo.extent.y, computeGroupSize));
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
