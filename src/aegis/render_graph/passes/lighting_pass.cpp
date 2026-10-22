module;
#include <imgui.h>

module aegis.render_passes;
import std;
import aegis.assets;
import aegis.core;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
LightingPass::LightingPass(ResourceDeclarations& declarations) :
    m_gBuffer{
        declarations.addReference(names::position, Usage::StorageRead),
        declarations.addReference(names::normal, Usage::StorageRead),
        declarations.addReference(names::albedo, Usage::StorageRead),
        declarations.addReference(names::arm, Usage::StorageRead),
        declarations.addReference(names::emissive, Usage::StorageRead),
    },
    m_sceneColor{ declarations.addTexture(names::sceneColor, Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT }) },
    m_uniforms{ declarations.addBuffer(names::lightingUniforms, Usage::HostWrite, { .size = sizeof(LightingUniforms), .perFrame = true }) }
{
}

auto LightingPass::name() const -> std::string_view
{
    return "Lighting";
}

auto LightingPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto pipeline = createComputePipeline(pool.device(), "Lighting", "lighting.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);

    auto ibl = Ibl::create(pool.device());
    if (!ibl)
        return std::unexpected{ ibl.error() };
    auto brdfLut = ibl->brdfLut();
    if (!brdfLut)
        return std::unexpected{ brdfLut.error() };
    m_brdfLut = std::move(*brdfLut);
    return {};
}

auto LightingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& scene = frameInfo.frameScene;
    const auto& environment = scene.environment;
    if (!environment.irradiance || !environment.prefiltered)
    {
        static bool reported{ false };
        if (!std::exchange(reported, true))
            log::warn("Lighting: the scene has no aegis::Sky with an environment; skipping lighting");
        return;
    }

    const auto uniforms = detail::packLighting(scene, m_ambientOcclusionFactor, m_viewMode);
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
        .irradiance = *environment.irradiance,
        .prefiltered = *environment.prefiltered,
        .brdfLut = m_brdfLut->descriptor(),
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

namespace detail
{
auto packLighting(const FrameScene& scene, float ambientOcclusionFactor, LightingViewMode viewMode) -> LightingUniforms
{
    LightingUniforms uniforms{
        .ambientOcclusionFactor = ambientOcclusionFactor,
        .viewMode = viewMode,
    };
    if (scene.camera)
        uniforms.cameraPosition = glm::vec4{ scene.camera->position, 1.0f };
    if (const auto& light = scene.ambientLight)
        uniforms.ambientColor = glm::vec4{ light->color, light->intensity };
    if (const auto& light = scene.directionalLight)
    {
        uniforms.directionalColor = glm::vec4{ light->color, light->intensity };
        uniforms.directionalDirection = glm::vec4{ light->direction, 0.0f };
    }
    for (const auto& light : scene.pointLights)
    {
        if (uniforms.pointLightCount == static_cast<std::int32_t>(maxPointLights))
            break;
        uniforms.pointLights[static_cast<std::size_t>(uniforms.pointLightCount++)] = {
            .position = glm::vec4{ light.position, 1.0f },
            .color = glm::vec4{ light.color, light.intensity },
        };
    }
    return uniforms;
}
}
}
