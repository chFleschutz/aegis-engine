module;
#include <cassert>

#include <imgui.h>

module aegis.render_passes;
import std;
import aegis.assets;
import aegis.core;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
// Light update ---------------------------------------------------------------------------------------

LightUpdatePass::LightUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene) :
    m_scene{ scene },
    m_lights{ declarations.addBuffer(names::lights, Usage::HostWrite, { .size = sizeof(LightData) * initialCapacity, .perFrame = true }) }
{
}

auto LightUpdatePass::name() const -> std::string_view
{
    return "Light Update";
}

auto LightUpdatePass::prepare(ResourcePool& pool) -> std::expected<void, Error>
{
    const auto& scene = m_scene.current();
    const auto count = static_cast<std::uint32_t>(scene.pointLights.size() + scene.spotLights.size());
    if (auto grown = detail::growBuffer(pool, m_lights, m_capacity, count, sizeof(LightData)); !grown)
        return std::unexpected{ std::move(grown.error()) };
    return {};
}

auto LightUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto lights = detail::packLights(m_scene.current());
    assert(lights.size() <= m_capacity && "The light buffer was not grown in prepare");
    if (!lights.empty())
        pool.write(m_lights, frameInfo.frameIndex, std::as_bytes(std::span{ lights }));
}

// Lighting -------------------------------------------------------------------------------------------

LightingPass::LightingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view,
    std::optional<ShadowMaps> shadow) :
    m_scene{ scene },
    m_view{ view },
    m_gBuffer{
        declarations.addReference(names::position, Usage::StorageRead),
        declarations.addReference(names::normal, Usage::StorageRead),
        declarations.addReference(names::albedo, Usage::StorageRead),
        declarations.addReference(names::arm, Usage::StorageRead),
        declarations.addReference(names::emissive, Usage::StorageRead),
    },
    m_sceneColor{ declarations.addTexture(names::sceneColor, Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT }) },
    m_uniforms{ declarations.addBuffer(names::lightingUniforms, Usage::HostWrite, { .size = sizeof(LightingUniforms), .perFrame = true }) },
    m_lights{ declarations.addReference(names::lights, Usage::ShaderRead) }
{
    if (shadow)
    {
        m_shadowView = shadow->view;
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
            m_shadowMaps[cascade] = declarations.addReference(shadow->maps[cascade], Usage::SampledCompute);
        if (!shadow->spotAtlas.empty())
            m_spotShadowAtlas = declarations.addReference(shadow->spotAtlas, Usage::SampledCompute);
    }
}

LightingPass::~LightingPass()
{
    if (m_shadowSampler)
        m_device->free(*m_shadowSampler);
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

    if (m_shadowView)
    {
        // A fetch compares the point's depth with the four nearest texels and blends the results: bilinear PCF. A point
        // outside the map is lit without a fetch (lighting::directionalShadow).
        m_device = &pool.device();
        auto sampler = m_device->createSampler({
            .name = "ShadowMapSampler",
            .mipmapMode = rhi::SamplerMipmapMode::Nearest,
            .addressMode = rhi::SamplerAddressMode::ClampToEdge,
            .maxAnisotropy = 1.0f,
            .compare = rhi::CompareOp::LessOrEqual,
        });
        if (!sampler)
            return makeError(ErrorCode::ResourceCreationFailed, "Failed to create the shadow map sampler", sampler.error());
        m_shadowSampler = *sampler;
    }
    return {};
}

auto LightingPass::prepare(ResourcePool& /*pool*/) -> std::expected<void, Error>
{
    const auto& environment = m_scene.current().environment;
    if (!environment.irradiance || !environment.prefiltered)
    {
        static bool reported{ false };
        if (!std::exchange(reported, true))
            log::warn("Lighting: the scene has no aegis::Sky with an environment; skipping lighting");
    }
    return {};
}

auto LightingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& scene = m_scene.current();
    const auto& environment = scene.environment;
    // Without one, prepare warned.
    if (!environment.irradiance || !environment.prefiltered)
        return;

    // The light update pass grew the buffer for this frame's lights.
    const auto lightCapacity = static_cast<std::uint32_t>(pool.bufferSize(m_lights) / sizeof(LightData));
    std::optional<detail::ShadowMapInput> shadow;
    if (m_shadowView)
    {
        shadow = detail::ShadowMapInput{ .view = *m_shadowView };
        const auto sampler = m_device->get(*m_shadowSampler).descriptor();
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
        {
            const auto& map = pool.texture(m_shadowMaps[cascade]);
            shadow->maps[cascade] = { *m_device->get(map.view).sampledDescriptor(), sampler };
        }
        if (m_spotShadowAtlas.isValid())
            shadow->spotAtlas = rhi::TextureDescriptor{ *m_device->get(pool.texture(m_spotShadowAtlas).view).sampledDescriptor(), sampler };
    }
    const auto uniforms = detail::packLighting(scene, m_view, pool.bufferAddress(m_lights, frameInfo.frameIndex),
        lightCapacity, m_ambientOcclusionFactor, m_viewMode, shadow, m_brdfLut->descriptor());
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
    };

    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(constants);
    const auto extent = pool.extent(m_sceneColor);
    cmd.dispatch(groupCount(extent.x, computeGroupSize), groupCount(extent.y, computeGroupSize));
}

auto LightingPass::drawUI() -> void
{
    ImGui::DragFloat("AO Factor", &m_ambientOcclusionFactor, 0.01f, 0.0f, 1.0f);
    static constexpr std::array viewModeNames{ "Scene Color", "Albedo", "Ambient Occlusion", "Roughness", "Metallic",
        "Emissive", "Shadow Cascades" };
    int mode = static_cast<int>(m_viewMode);
    if (ImGui::Combo("View Mode", &mode, viewModeNames.data(), static_cast<int>(viewModeNames.size())))
        m_viewMode = static_cast<LightingViewMode>(mode);
}

namespace detail
{
auto packLight(const FrameScene::PointLight& light) -> LightData
{
    return {
        .position = light.position,
        .range = light.range,
        .color = light.color,
        .intensity = light.intensity,
        .direction = glm::vec3{ 0.0f },
        .coneScale = 0.0f,
        .coneOffset = 1.0f,
    };
}

auto packLight(const FrameScene::SpotLight& light) -> LightData
{
    // glTF's KHR_lights_punctual: the cone's falloff is linear in the cosine between the inner and the outer cone.
    const float cosOuter = std::cos(light.outerConeAngle);
    const float cosInner = std::max(std::cos(light.innerConeAngle), cosOuter);
    const float coneScale = 1.0f / std::max(cosInner - cosOuter, 1e-3f);
    return {
        .position = light.position,
        .range = light.range,
        .color = light.color,
        .intensity = light.intensity,
        .direction = light.direction,
        .coneScale = coneScale,
        .coneOffset = -cosOuter * coneScale,
    };
}

auto packSpotShadow(LightData& data, const FrameScene::SpotLight& light, const FrameScene::Camera& camera, rhi::Rect2D tile) -> void
{
    const auto atlas = glm::vec2{ static_cast<float>(spotShadowAtlasExtent.x), static_cast<float>(spotShadowAtlasExtent.y) };
    data.shadowViewProjection = glm::rowMajor4(camera.projection * camera.view);
    data.shadowRegion = glm::vec4{ static_cast<float>(tile.x) / atlas.x, static_cast<float>(tile.y) / atlas.y,
        static_cast<float>(tile.extent.x) / atlas.x, static_cast<float>(tile.extent.y) / atlas.y };
    data.shadowBias = light.shadowBias;
    // The perspective projection spans 2 / projection[0][0] meters across the tile one meter along the light's axis.
    data.shadowTexelSize = 2.0f / (std::abs(camera.projection[0][0]) * static_cast<float>(std::max(tile.extent.x, 1u)));
}

auto packLights(const FrameScene& scene) -> std::vector<LightData>
{
    std::vector<LightData> lights;
    lights.reserve(scene.pointLights.size() + scene.spotLights.size());
    for (const auto& light : scene.pointLights)
        lights.push_back(packLight(light));
    for (const auto& light : scene.spotLights)
    {
        auto& data = lights.emplace_back(packLight(light));
        if (!light.shadowView || *light.shadowView >= scene.views.size())
            continue;
        const auto& view = scene.views[*light.shadowView];
        if (view.camera && view.tile)
            packSpotShadow(data, light, *view.camera, *view.tile);
    }
    return lights;
}

auto packLighting(const FrameScene& scene, std::uint32_t view, rhi::DeviceAddress lights, std::uint32_t lightCapacity,
    float ambientOcclusionFactor, LightingViewMode viewMode, std::optional<ShadowMapInput> shadow,
    rhi::TextureDescriptor brdfLut) -> LightingUniforms
{
    const auto pointCount = std::min(static_cast<std::uint32_t>(scene.pointLights.size()), lightCapacity);
    const auto spotCount = std::min(static_cast<std::uint32_t>(scene.spotLights.size()), lightCapacity - pointCount);
    LightingUniforms uniforms{
        .lights = lights,
        .pointLightCount = pointCount,
        .spotLightCount = spotCount,
        .ambientOcclusionFactor = ambientOcclusionFactor,
        .viewMode = viewMode,
        .irradiance = scene.environment.irradiance.value_or(rhi::TextureDescriptor{}),
        .prefiltered = scene.environment.prefiltered.value_or(rhi::TextureDescriptor{}),
        .brdfLut = brdfLut,
    };
    if (const auto camera = scene.camera(view))
        uniforms.cameraPosition = glm::vec4{ camera->position, 1.0f };
    if (const auto& light = scene.ambientLight)
        uniforms.ambientColor = glm::vec4{ light->color, light->intensity };
    if (const auto& light = scene.directionalLight)
    {
        uniforms.directionalColor = glm::vec4{ light->color, light->intensity };
        uniforms.directionalDirection = glm::vec4{ light->direction, 0.0f };
        const auto camera = scene.camera(view);
        if (shadow && camera)
        {
            // The cascades split the main view's camera axis; the shadow views hold their fitted cameras.
            uniforms.cameraForward = glm::vec4{ -camera->view[0][2], -camera->view[1][2], -camera->view[2][2], 0.0f };
            uniforms.shadowBias = light->shadowBias;
            uniforms.shadowCascadeBlend = shadowCascadeBlend;
            const auto splits = shadowCascadeSplits(*light, camera->projection);
            for (std::uint32_t cascade = 0; cascade < splits.size(); ++cascade)
            {
                const auto shadowCamera = scene.camera(shadow->view + cascade);
                if (!shadowCamera)
                    break;
                // The orthographic projection spans 2 / projection[0][0] meters across the map.
                const auto extent = scene.views[shadow->view + cascade].extent.value_or(shadowMapExtent);
                uniforms.shadowCascades[cascade] = {
                    .viewProjection = glm::rowMajor4(shadowCamera->projection * shadowCamera->view),
                    .map = shadow->maps[cascade],
                    .texelSize = 2.0f / (std::abs(shadowCamera->projection[0][0]) * static_cast<float>(std::max(extent.x, 1u))),
                    .splitFar = splits[cascade],
                };
                uniforms.shadowCascadeCount = cascade + 1;
            }
        }
    }
    if (shadow && shadow->spotAtlas)
    {
        uniforms.spotShadowAtlas = *shadow->spotAtlas;
        uniforms.spotShadows = 1;
    }
    return uniforms;
}
}
}
