module;
#include <cassert>

module aegis.render_passes;
import std;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
// Temporal anti-aliasing -----------------------------------------------------------------------------

TemporalAaPass::TemporalAaPass(ResourceDeclarations& declarations, const FrameSceneSource& scene,
    PipelineLibrary& pipelines, Settings& settings, std::uint32_t view) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_settings{ settings },
    m_view{ view },
    m_sceneColor{ declarations.addReference(names::sceneColor, Usage::StorageRead) },
    // Depth and motion are fetched texel by texel (Texture2D::Load): a depth format has no storage view.
    m_depth{ declarations.addReference(names::depth, Usage::SampledCompute) },
    m_motion{ declarations.addReference(names::motionVectors, Usage::SampledCompute) },
    m_temporalColor{ declarations.addTexture(names::temporalColor, Usage::StorageWrite,
        { .format = rhi::Format::RGBA16_SFLOAT, .lifetime = Lifetime::History }) },
    m_history{ declarations.addHistoryReference(names::temporalColor, Usage::SampledCompute) }
{
}

TemporalAaPass::~TemporalAaPass()
{
    if (m_sampler)
        m_samplerDevice->free(*m_sampler);
}

auto TemporalAaPass::name() const -> std::string_view
{
    return "Temporal AA";
}

auto TemporalAaPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto& device = pool.device();
    auto pipeline = m_pipelines.compute(device, "TemporalAa", "temporal_aa.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = *pipeline;

    // Bilinear, for the Catmull-Rom filter's taps between texels.
    auto sampler = device.createSampler({
        .name = "TemporalAaSampler",
        .addressMode = rhi::SamplerAddressMode::ClampToEdge,
        .maxAnisotropy = 1.0f,
    });
    if (!sampler)
        return makeError(ErrorCode::ResourceCreationFailed, "Failed to create the temporal AA sampler", sampler.error());
    m_sampler = *sampler;
    m_samplerDevice = &device;
    return {};
}

auto TemporalAaPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& device = pool.device();
    const auto sampled = [&](ResourceId id) { return *device.get(pool.texture(id).view).sampledDescriptor(); };
    const auto camera = m_scene.current().camera(m_view);
    // Without a camera the view draws nothing worth keeping: start over once it has one.
    const bool cut = !camera || camera->cut;

    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(TemporalAaConstants{
        .output = pool.storageDescriptor(m_temporalColor),
        .sceneColor = pool.storageDescriptor(m_sceneColor),
        .depth = sampled(m_depth),
        .motion = sampled(m_motion),
        .history = { sampled(m_history), device.get(*m_sampler).descriptor() },
        .historyWeight = detail::historyWeight(pool.isFresh(m_temporalColor), cut, m_settings),
    });
    const auto extent = pool.extent(m_temporalColor);
    cmd.dispatch(groupCount(extent.x, computeGroupSize), groupCount(extent.y, computeGroupSize));
}

auto TemporalAaPass::settings() -> std::optional<PassSettings>
{
    const auto* type = passSettingsTypes().find<Settings>();
    assert(type && "TemporalAaPass::Settings is registered in passSettingsTypes");
    return PassSettings{ .type = *type, .object = &m_settings };
}

// Bloom ----------------------------------------------------------------------------------------------

BloomPass::BloomPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
    Settings& settings) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_settings{ settings },
    m_temporalColor{ declarations.addReference(names::temporalColor, Usage::StorageRead) },
    m_bloom{ declarations.addTexture(names::bloom, Usage::StorageWrite,
        { .format = rhi::Format::RGBA16_SFLOAT, .mipLevels = mipLevels, .usage = rhi::ImageUsage::Sampled }) }
{
}

BloomPass::~BloomPass()
{
    if (m_sampler)
        m_samplerDevice->free(*m_sampler);
}

auto BloomPass::name() const -> std::string_view
{
    return "Bloom";
}

auto BloomPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto& device = pool.device();
    for (auto [pipeline, name, path] : {
             std::tuple{ &m_threshold, "BloomThreshold", "bloom/threshold.slang.spv" },
             std::tuple{ &m_downsample, "BloomDownsample", "bloom/downsample.slang.spv" },
             std::tuple{ &m_upsample, "BloomUpsample", "bloom/upsample.slang.spv" },
         })
    {
        auto found = m_pipelines.compute(device, name, path);
        if (!found)
            return std::unexpected{ found.error() };
        *pipeline = *found;
    }

    auto sampler = device.createSampler({
        .name = "BloomSampler",
        .addressMode = rhi::SamplerAddressMode::ClampToEdge,
        .maxAnisotropy = 1.0f,
    });
    if (!sampler)
        return makeError(ErrorCode::ResourceCreationFailed, "Failed to create the bloom sampler", sampler.error());
    m_sampler = *sampler;
    m_samplerDevice = &device;
    return {};
}

auto BloomPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& device = pool.device();
    const auto view = [&](std::uint32_t mip) { return pool.mipView(m_bloom, mip); };
    const auto storage = [&](std::uint32_t mip) { return *device.get(view(mip)).storageDescriptor(); };
    const auto sampled = [&](std::uint32_t mip) {
        return rhi::TextureDescriptor{ *device.get(view(mip)).sampledDescriptor(), device.get(*m_sampler).descriptor() };
    };
    const auto mipExtent = [&](std::uint32_t mip) { return device.get(view(mip)).extent(); };
    const auto dispatchMip = [&](std::uint32_t mip) {
        frameInfo.cmd.dispatch(groupCount(mipExtent(mip).x, computeGroupSize), groupCount(mipExtent(mip).y, computeGroupSize));
    };

    const auto& cmd = frameInfo.cmd;
    const auto& settings = m_scene.current().postProcess;

    // Bright regions into mip 0. The graph left every mip in ComputeStorage.
    cmd.bindPipeline(*m_threshold);
    cmd.pushConstants(BloomThresholdConstants{
        .input = pool.storageDescriptor(m_temporalColor),
        .output = storage(0),
        .threshold = settings.bloomThreshold,
        .softThreshold = settings.bloomSoftThreshold,
    });
    dispatchMip(0);

    // Down the chain: each mip is read (sampled) once the next one is written from it.
    cmd.bindPipeline(*m_downsample);
    // The image has fewer mips than requested when the window is tiny.
    const auto mipCount = device.get(pool.texture(m_bloom).image).mipLevels();
    for (std::uint32_t mip = 1; mip < mipCount; ++mip)
    {
        cmd.transitionImageLayout(view(mip - 1), rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderReadCompute);
        cmd.pushConstants(BloomDownsampleConstants{
            .output = storage(mip),
            .input = sampled(mip - 1),
            .mipLevel = static_cast<std::int32_t>(mip - 1),
            .filterScale = m_settings.downsampleScale,
        });
        dispatchMip(mip);
    }
    cmd.transitionImageLayout(view(mipCount - 1), rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderReadCompute);

    // Back up: each mip is rewritten from the one below.
    cmd.bindPipeline(*m_upsample);
    for (auto mip = mipCount - 1; mip > 0; --mip)
    {
        cmd.transitionImageLayout(view(mip - 1), rhi::ResourceState::ShaderReadCompute, rhi::ResourceState::ComputeStorage);
        cmd.pushConstants(BloomUpsampleConstants{
            .output = storage(mip - 1),
            .input = sampled(mip),
            .filterScale = m_settings.upsampleScale,
        });
        dispatchMip(mip - 1);
        cmd.transitionImageLayout(view(mip - 1), rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderReadCompute);
    }

    // Leave the state the graph expects after a StorageWrite.
    cmd.transitionImageLayout(pool.texture(m_bloom).image, rhi::ResourceState::ShaderReadCompute,
        rhi::ResourceState::ComputeStorage);
}

auto BloomPass::settings() -> std::optional<PassSettings>
{
    const auto* type = passSettingsTypes().find<Settings>();
    assert(type && "BloomPass::Settings is registered in passSettingsTypes");
    return PassSettings{ .type = *type, .object = &m_settings };
}

// Transparent ----------------------------------------------------------------------------------------

TransparentPass::TransparentPass(ResourceDeclarations& declarations, const FrameSceneSource& scene,
    PipelineLibrary& pipelines, std::uint32_t view) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_view{ view },
    m_sceneColor{ declarations.addReference(names::sceneColor, Usage::ColorAttachment) },
    m_depth{ declarations.addReference(names::depth, Usage::DepthRead) },
    m_cameraData{ declarations.addReference(names::cameraData, Usage::ShaderRead) }
{
}

auto TransparentPass::name() const -> std::string_view
{
    return "Transparent";
}

auto TransparentPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    const std::array colorAttachments{ rhi::Pipeline::ColorAttachment{ .format = pool.format(m_sceneColor) } };
    for (const auto& system : m_renderSystems)
    {
        if (auto created = system->createResources(m_pipelines, pool.device(), { colorAttachments, pool.format(m_depth) }); !created)
            return created;
    }
    return {};
}

auto TransparentPass::prepare(ResourcePool& /*pool*/) -> std::expected<void, Error>
{
    for (const auto& system : m_renderSystems)
    {
        if (auto prepared = system->prepare(m_scene.current()); !prepared)
            return prepared;
    }
    return {};
}

auto TransparentPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ rhi::Attachment::colorLoad(pool.texture(m_sceneColor).view) };
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depthReadOnly(pool.texture(m_depth).view),
    });
    cmd.setViewport(pool.extent(m_depth));
    cmd.setScissor(pool.extent(m_depth));

    const auto& scene = m_scene.current();
    const auto camera = scene.camera(m_view);
    const RenderContext context{
        .cmd = cmd,
        .frameScene = scene,
        .frameIndex = frameInfo.frameIndex,
        .camera = pool.bufferAddress(m_cameraData),
        .view = camera ? camera->view : math::Float4x4::identity(),
    };
    for (const auto& system : m_renderSystems)
        system->render(context);

    cmd.endRendering();
}

// Point lights ---------------------------------------------------------------------------------------

auto PointLightRenderSystem::createResources(PipelineLibrary& pipelines, rhi::Device& device, const RenderTargets& targets)
    -> std::expected<void, Error>
{
    std::vector<rhi::Pipeline::ColorAttachment> blended(targets.colorAttachments.begin(), targets.colorAttachments.end());
    for (auto& attachment : blended)
        attachment.blend = rhi::BlendMode::Alpha;

    auto pipeline = pipelines.graphics(device, "point_light.slang.spv",
        rhi::Pipeline::GraphicsDesc{
            .name = "PointLights",
            .colorAttachments = blended,
            .depthAttachment = targets.depthAttachment,
            .depth = { .test = true, .write = false },
            .cullMode = rhi::CullMode::None,
        });
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = *pipeline;
    return {};
}

auto PointLightRenderSystem::render(const RenderContext& context) -> void
{
    // The billboards blend, so the farthest draws first.
    const auto& lights = context.frameScene.pointLights;
    m_order.clear();
    for (std::uint32_t i = 0; i < lights.size(); ++i)
        m_order.push_back({ .depth = detail::viewDepth(context.view, lights[i].position), .index = i });
    detail::sortBackToFront(m_order);

    context.cmd.bindPipeline(*m_pipeline);
    for (const auto& draw : m_order)
    {
        context.cmd.pushConstants(detail::makePointLightConstants(lights[draw.index], context.camera));
        context.cmd.draw(6);
    }
}
}

namespace aegis::render_graph::detail
{
auto historyWeight(bool fresh, bool cut, const TemporalAaPass::Settings& settings) -> float
{
    if (fresh || cut || !settings.enabled)
        return 0.0f;
    return 1.0f - std::clamp(settings.currentWeight, 0.0f, 1.0f);
}

auto makePointLightConstants(const FrameScene::PointLight& light, rhi::DeviceAddress camera) -> PointLightConstants
{
    constexpr float pointLightScale{ 0.002f };
    return {
        .worldPosition = math::Float4{ light.position, 1.0f },
        .color = math::Float4{ light.color, 1.0f },
        .radius = light.intensity * light.scale * pointLightScale,
        .camera = camera,
    };
}
}
