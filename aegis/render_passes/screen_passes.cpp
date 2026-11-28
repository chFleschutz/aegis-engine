module aegis.render_passes;
import std;
import aegis.assets;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
// Sky box -------------------------------------------------------------------------------------------

SkyBoxPass::SkyBoxPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
    std::uint32_t view) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_view{ view },
    m_sceneColor{ declarations.addReference(names::sceneColor, Usage::ColorAttachment) },
    m_depth{ declarations.addReference(names::depth, Usage::DepthRead) }
{
}

auto SkyBoxPass::name() const -> std::string_view
{
    return "Sky Box";
}

auto SkyBoxPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    const std::array colorAttachments{ rhi::Pipeline::ColorAttachment{ .format = pool.format(m_sceneColor) } };
    auto pipeline = m_pipelines.graphics(pool.device(), "sky_box.slang.spv",
        rhi::Pipeline::GraphicsDesc{
            .name = "SkyBox",
            .colorAttachments = colorAttachments,
            .depthAttachment = pool.format(m_depth),
            .depth = { .test = true, .write = false, .compareOp = rhi::CompareOp::LessOrEqual },
            .cullMode = rhi::CullMode::Front, // Seen from inside
        });
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = *pipeline;
    return {};
}

auto SkyBoxPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& scene = m_scene.current();
    const auto camera = scene.camera(m_view);
    if (!camera || !scene.environment.skybox)
        return;

    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ rhi::Attachment::colorLoad(pool.texture(m_sceneColor).view) };
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depthReadOnly(pool.texture(m_depth).view),
    });
    cmd.setViewport(pool.extent(m_depth));
    cmd.setScissor(pool.extent(m_depth));
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(SkyBoxConstants{
        .viewProjection = detail::makeSkyBoxViewProjection(*camera),
        .skybox = *scene.environment.skybox,
    });
    cmd.draw(36);
    cmd.endRendering();
}

// Post-processing -----------------------------------------------------------------------------------

PostProcessingPass::PostProcessingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene,
    PipelineLibrary& pipelines, Desc desc) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_color{ declarations.addReference(names::temporalColor, Usage::StorageRead) },
    m_bloom{ desc.bloom ? std::optional{ declarations.addReference(names::bloom, Usage::StorageRead) } : std::nullopt },
    // 16-bit: the image may stay linear until the blit onto an sRGB target encodes it (see execute). As a
    // view's target it is also copied out, for a screenshot.
    m_final{ declarations.addTexture(names::finalImage, Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT, .usage = rhi::ImageUsage::TransferSrc }) },
    m_targetEncodes{ desc.targetEncodes }
{
}

auto PostProcessingPass::name() const -> std::string_view
{
    return "Post Processing";
}

auto PostProcessingPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto pipeline = m_pipelines.compute(pool.device(), "PostProcessing", "post_process.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = *pipeline;
    return {};
}

auto PostProcessingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& settings = m_scene.current().postProcess;
    const auto color = pool.storageDescriptor(m_color);
    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(PostProcessConstants{
        .final = pool.storageDescriptor(m_final),
        .color = color,
        .bloom = m_bloom ? pool.storageDescriptor(*m_bloom) : color,
        .toneMapping = settings.toneMapping,
        .bloomIntensity = m_bloom ? settings.bloomIntensity : 0.0f,
        .exposure = settings.exposure,
        // An sRGB target (the swapchain prefers one) encodes on write. A view's "Final" reaches the same target
        // through the UI, so the same holds for it.
        .gamma = m_targetEncodes ? 1.0f : settings.gamma,
    });
    const auto extent = pool.extent(m_final);
    cmd.dispatch(groupCount(extent.x, computeGroupSize), groupCount(extent.y, computeGroupSize));
}

// Present -------------------------------------------------------------------------------------------

PresentPass::PresentPass(ResourceDeclarations& declarations, rhi::Format targetFormat, std::string_view source) :
    m_source{ declarations.addReference(source, Usage::TransferSrc) },
    m_target{ declareFrameTarget(declarations, Usage::TransferDst, targetFormat) }
{
}

auto PresentPass::name() const -> std::string_view
{
    return "Present";
}

auto PresentPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    frameInfo.cmd.blit(pool.texture(m_source).view, pool.texture(m_target).view);
}
}

namespace aegis::render_graph::detail
{
auto makeSkyBoxViewProjection(const FrameScene::Camera& camera) -> math::Float4x4
{
    const auto viewRotation = math::Float4x4{ math::Float3x3{ camera.view } };
    return camera.projection * viewRotation;
}
}
