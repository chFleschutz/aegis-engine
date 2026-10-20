module;
#include <imgui.h>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.rhi;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
// Sky box -------------------------------------------------------------------------------------------

SkyBoxPass::SkyBoxPass(ResourcePool& pool) :
    m_sceneColor{ pool.addReference(names::sceneColor, Usage::ColorAttachment) },
    m_depth{ pool.addReference(names::depth, Usage::DepthRead) }
{
}

auto SkyBoxPass::info() const -> Info
{
    return { .name = "Sky Box", .reads = { m_depth }, .writes = { m_sceneColor } };
}

auto SkyBoxPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    const std::array colorAttachments{ rhi::Pipeline::ColorAttachment{ .format = rhi::Format::RGBA16_SFLOAT } };
    auto pipeline = createGraphicsPipeline(pool.device(), "sky_box.slang.spv",
        rhi::Pipeline::GraphicsDesc{
            .name = "SkyBox",
            .colorAttachments = colorAttachments,
            .depthAttachment = rhi::Format::D32_SFLOAT,
            .depth = { .test = true, .write = false, .compareOp = rhi::CompareOp::LessOrEqual },
            .cullMode = rhi::CullMode::Front, // Seen from inside
        });
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto SkyBoxPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    auto& registry = frameInfo.scene.registry();
    const auto environment = frameInfo.scene.environment();
    const auto camera = frameInfo.scene.mainCamera();
    if (!environment || !registry.has<assets::Environment>(environment) || !camera || !registry.has<aegis::Camera>(camera))
        return;
    const auto& skybox = registry.get<assets::Environment>(environment).skybox;
    if (!skybox)
        return;

    // Rotation only: the sky is infinitely far away.
    const auto& cameraComponent = registry.get<aegis::Camera>(camera);
    const auto viewRotation = glm::mat4{ glm::mat3{ cameraComponent.viewMatrix } };
    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ rhi::Attachment::colorLoad(pool.texture(m_sceneColor).view) };
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depthReadOnly(pool.texture(m_depth).view),
    });
    cmd.setViewport(frameInfo.extent);
    cmd.setScissor(frameInfo.extent);
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(SkyBoxConstants{
        .viewProjection = glm::rowMajor4(cameraComponent.projectionMatrix * viewRotation),
        .skybox = skybox->descriptor(),
    });
    cmd.draw(36);
    cmd.endRendering();
}

// Post-processing -----------------------------------------------------------------------------------

PostProcessingPass::PostProcessingPass(ResourcePool& pool, bool bloom) :
    m_sceneColor{ pool.addReference(names::sceneColor, Usage::StorageRead) },
    m_bloom{ bloom ? std::optional{ pool.addReference(names::bloom, Usage::StorageRead) } : std::nullopt },
    // 16-bit: the image may stay linear until the blit onto an sRGB target encodes it (see execute).
    m_final{ pool.addTexture(names::finalImage, Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT }) }
{
}

auto PostProcessingPass::info() const -> Info
{
    Info info{ .name = "Post Processing", .reads = { m_sceneColor }, .writes = { m_final } };
    if (m_bloom)
        info.reads.push_back(*m_bloom);
    return info;
}

auto PostProcessingPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto pipeline = createComputePipeline(pool.device(), "PostProcessing", "post_process.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto PostProcessingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // An sRGB target (the swapchain prefers one) encodes on write; gamma here would apply it twice.
    const auto targetFormat = pool.device().get(frameInfo.target).format();
    const bool targetEncodes = targetFormat == rhi::Format::BGRA8_SRGB || targetFormat == rhi::Format::RGBA8_SRGB;

    const auto sceneColor = pool.storageDescriptor(m_sceneColor);
    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(PostProcessConstants{
        .final = pool.storageDescriptor(m_final),
        .sceneColor = sceneColor,
        .bloom = m_bloom ? pool.storageDescriptor(*m_bloom) : sceneColor,
        .toneMapping = m_settings.toneMapping,
        .bloomIntensity = m_bloom ? m_settings.bloomIntensity : 0.0f,
        .exposure = m_settings.exposure,
        .gamma = targetEncodes ? 1.0f : m_settings.gamma,
    });
    cmd.dispatch(groupCount(frameInfo.extent.x, computeGroupSize), groupCount(frameInfo.extent.y, computeGroupSize));
}

auto PostProcessingPass::drawUI() -> void
{
    static constexpr std::array toneMappingNames{ "Reinhard", "ACES" };
    int mode = static_cast<int>(m_settings.toneMapping);
    if (ImGui::Combo("Tone Mapping", &mode, toneMappingNames.data(), static_cast<int>(toneMappingNames.size())))
        m_settings.toneMapping = static_cast<ToneMapping>(mode);
    ImGui::SliderFloat("Bloom Intensity", &m_settings.bloomIntensity, 0.0f, 2.0f);
    ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.1f, 10.0f);
    ImGui::SliderFloat("Gamma", &m_settings.gamma, 1.0f, 5.0f);
}

// Present -------------------------------------------------------------------------------------------

PresentPass::PresentPass(ResourcePool& pool, std::string_view source) :
    m_source{ pool.addReference(source, Usage::TransferSrc) }
{
}

auto PresentPass::info() const -> Info
{
    return { .name = "Present", .reads = { m_source } };
}

auto PresentPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // The target arrives as a color attachment and must leave as one (renderer::Renderer's contract).
    const auto& cmd = frameInfo.cmd;
    cmd.transitionImageLayout(frameInfo.target, rhi::ResourceState::ColorAttachment, rhi::ResourceState::CopyDst);
    cmd.blit(pool.texture(m_source).view, frameInfo.target);
    cmd.transitionImageLayout(frameInfo.target, rhi::ResourceState::CopyDst, rhi::ResourceState::ColorAttachment);
}
}
