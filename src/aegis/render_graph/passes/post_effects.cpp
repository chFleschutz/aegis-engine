module;
#include <imgui.h>

module aegis.render_graph;
import std;
import aegis.rhi;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
// Bloom ----------------------------------------------------------------------------------------------

BloomPass::BloomPass(ResourcePool& pool) :
    m_sceneColor{ pool.addReference("SceneColor", Usage::StorageRead) },
    m_bloom{ pool.addTexture("Bloom", Usage::StorageWrite,
        { .format = rhi::Format::RGBA16_SFLOAT, .mipLevels = mipLevels, .usage = rhi::ImageUsage::Sampled }) }
{
}

BloomPass::~BloomPass()
{
    freeMipViews();
    if (m_sampler)
        m_device->free(*m_sampler);
}

auto BloomPass::info() const -> Info
{
    return { .name = "Bloom", .reads = { m_sceneColor }, .writes = { m_bloom } };
}

auto BloomPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    m_device = &pool.device();

    for (auto [pipeline, name, path] : {
             std::tuple{ &m_threshold, "BloomThreshold", "bloom/threshold.slang.spv" },
             std::tuple{ &m_downsample, "BloomDownsample", "bloom/downsample.slang.spv" },
             std::tuple{ &m_upsample, "BloomUpsample", "bloom/upsample.slang.spv" },
         })
    {
        auto created = createComputePipeline(*m_device, name, path);
        if (!created)
            return std::unexpected{ created.error() };
        pipeline->emplace(std::move(*created));
    }

    auto sampler = m_device->createSampler({
        .name = "BloomSampler",
        .addressMode = rhi::SamplerAddressMode::ClampToEdge,
        .maxAnisotropy = 1.0f,
    });
    if (!sampler)
        return makeError(sampler.error(), "Failed to create the bloom sampler");
    m_sampler = *sampler;

    return createMipViews(pool);
}

auto BloomPass::resized(ResourcePool& pool) -> std::expected<void, Error>
{
    // The renderer replaced the image behind "Bloom"; the mip views still see the old one.
    freeMipViews();
    return createMipViews(pool);
}

auto BloomPass::createMipViews(ResourcePool& pool) -> std::expected<void, Error>
{
    // The image has fewer mips than requested when the window is tiny.
    const auto& image = m_device->get(pool.texture(m_bloom).image);
    for (std::uint32_t mip = 0; mip < image.mipLevels(); ++mip)
    {
        auto view = m_device->createImageView(pool.texture(m_bloom).image,
            rhi::ImageView::Desc{
                .name = std::format("BloomMip{}", mip),
                .extent = { std::max(image.extent().x >> mip, 1u), std::max(image.extent().y >> mip, 1u), 1 },
                .format = image.format(),
                .range = { .baseMipLevel = mip, .mipLevelCount = 1 },
                .usage = rhi::ImageUsage::Storage | rhi::ImageUsage::Sampled,
            });
        if (!view)
            return makeError(view.error(), "Failed to create a bloom mip view");
        m_mipViews.push_back(*view);
    }
    return {};
}

auto BloomPass::freeMipViews() -> void
{
    for (const auto view : m_mipViews)
        m_device->free(view);
    m_mipViews.clear();
}

auto BloomPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto storage = [&](rhi::ImageViewHandle view) { return *m_device->get(view).storageDescriptor(); };
    const auto sampled = [&](rhi::ImageViewHandle view) {
        return rhi::TextureDescriptor{ *m_device->get(view).sampledDescriptor(), m_device->get(*m_sampler).descriptor() };
    };
    const auto mipExtent = [&](std::uint32_t mip) { return m_device->get(m_mipViews[mip]).extent(); };
    const auto dispatchMip = [&](std::uint32_t mip) {
        frameInfo.cmd.dispatch(groupCount(mipExtent(mip).x, computeGroupSize), groupCount(mipExtent(mip).y, computeGroupSize));
    };

    const auto& cmd = frameInfo.cmd;

    // Bright regions into mip 0. The graph left every mip in ComputeStorage.
    cmd.bindPipeline(*m_threshold);
    cmd.pushConstants(BloomThresholdConstants{
        .input = pool.storageDescriptor(m_sceneColor),
        .output = storage(m_mipViews[0]),
        .threshold = m_settings.threshold,
        .softThreshold = m_settings.softThreshold,
    });
    dispatchMip(0);

    // Down the chain: each mip is read (sampled) once the next one is written from it.
    cmd.bindPipeline(*m_downsample);
    const auto mipCount = static_cast<std::uint32_t>(m_mipViews.size());
    for (std::uint32_t mip = 1; mip < mipCount; ++mip)
    {
        cmd.transitionImageLayout(m_mipViews[mip - 1], rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderReadCompute);
        cmd.pushConstants(BloomDownsampleConstants{
            .output = storage(m_mipViews[mip]),
            .input = sampled(m_mipViews[mip - 1]),
            .mipLevel = static_cast<std::int32_t>(mip - 1),
            .filterScale = m_settings.downsampleScale,
        });
        dispatchMip(mip);
    }
    cmd.transitionImageLayout(m_mipViews[mipCount - 1], rhi::ResourceState::ComputeStorage,
        rhi::ResourceState::ShaderReadCompute);

    // Back up: each mip is rewritten from the one below.
    cmd.bindPipeline(*m_upsample);
    for (auto mip = mipCount - 1; mip > 0; --mip)
    {
        cmd.transitionImageLayout(m_mipViews[mip - 1], rhi::ResourceState::ShaderReadCompute, rhi::ResourceState::ComputeStorage);
        cmd.pushConstants(BloomUpsampleConstants{
            .output = storage(m_mipViews[mip - 1]),
            .input = sampled(m_mipViews[mip]),
            .filterScale = m_settings.upsampleScale,
        });
        dispatchMip(mip - 1);
        cmd.transitionImageLayout(m_mipViews[mip - 1], rhi::ResourceState::ComputeStorage, rhi::ResourceState::ShaderReadCompute);
    }

    // Leave the state the graph expects after a StorageWrite.
    cmd.transitionImageLayout(pool.texture(m_bloom).image, rhi::ResourceState::ShaderReadCompute,
        rhi::ResourceState::ComputeStorage);
}

auto BloomPass::drawUI() -> void
{
    ImGui::SliderFloat("Threshold", &m_settings.threshold, 0.0f, 2.0f);
    ImGui::SliderFloat("Soft Threshold", &m_settings.softThreshold, 0.0f, 2.0f);
    ImGui::SliderFloat("Downsample Scale", &m_settings.downsampleScale, 0.0f, 2.0f);
    ImGui::SliderFloat("Upsample Scale", &m_settings.upsampleScale, 0.0f, 2.0f);
}

// Transparent ----------------------------------------------------------------------------------------

TransparentPass::TransparentPass(ResourcePool& pool) :
    m_sceneColor{ pool.addReference("SceneColor", Usage::ColorAttachment) },
    m_depth{ pool.addReference("Depth", Usage::DepthRead) },
    m_cameraData{ pool.addReference("CameraData", Usage::ShaderRead) }
{
}

auto TransparentPass::info() const -> Info
{
    return { .name = "Transparent", .reads = { m_depth, m_cameraData }, .writes = { m_sceneColor } };
}

auto TransparentPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    const std::array colorAttachments{ rhi::Pipeline::ColorAttachment{ .format = rhi::Format::RGBA16_SFLOAT } };
    for (const auto& system : m_renderSystems)
    {
        if (auto created = system->createResources(pool.device(), { colorAttachments, rhi::Format::D32_SFLOAT }); !created)
            return created;
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
    cmd.setViewport(frameInfo.extent);
    cmd.setScissor(frameInfo.extent);

    const RenderContext context{
        .cmd = cmd,
        .scene = frameInfo.scene,
        .frameIndex = frameInfo.frameIndex,
        .camera = pool.bufferAddress(m_cameraData, frameInfo.frameIndex),
    };
    for (const auto& system : m_renderSystems)
        system->render(context);

    cmd.endRendering();
}

// Point lights ---------------------------------------------------------------------------------------

auto PointLightRenderSystem::createResources(rhi::Device& device, const RenderTargets& targets)
    -> std::expected<void, Error>
{
    std::vector<rhi::Pipeline::ColorAttachment> blended(targets.colorAttachments.begin(), targets.colorAttachments.end());
    for (auto& attachment : blended)
        attachment.blend = rhi::BlendMode::Alpha;

    auto pipeline = createGraphicsPipeline(device, "point_light.slang.spv",
        rhi::Pipeline::GraphicsDesc{
            .name = "PointLights",
            .colorAttachments = blended,
            .depthAttachment = targets.depthAttachment,
            .depth = { .test = true, .write = false },
            .cullMode = rhi::CullMode::None,
        });
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto PointLightRenderSystem::render(const RenderContext& context) -> void
{
    constexpr float pointLightScale{ 0.002f };

    context.cmd.bindPipeline(*m_pipeline);
    for (auto&& [entity, transform, light] : context.scene.registry().view<aegis::GlobalTransform, aegis::PointLight>().each())
    {
        context.cmd.pushConstants(PointLightConstants{
            .worldPosition = glm::vec4{ transform.location, 1.0f },
            .color = glm::vec4{ light.color, 1.0f },
            .radius = light.intensity * transform.scale.x * pointLightScale,
            .camera = context.camera,
        });
        context.cmd.draw(6);
    }
}
}
