module;
#include <imgui.h>

module aegis.render_passes;
import std;
import aegis.assets;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
// Sky box -------------------------------------------------------------------------------------------

SkyBoxPass::SkyBoxPass(ResourceDeclarations& declarations) :
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
    auto pipeline = createGraphicsPipeline(pool.device(), "sky_box.slang.spv",
        rhi::Pipeline::GraphicsDesc{
            .name = "SkyBox",
            .colorAttachments = colorAttachments,
            .depthAttachment = pool.format(m_depth),
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
    const auto& scene = frameInfo.frameScene;
    if (!scene.camera || !scene.environment.skybox)
        return;

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
        .viewProjection = detail::makeSkyBoxViewProjection(*scene.camera),
        .skybox = *scene.environment.skybox,
    });
    cmd.draw(36);
    cmd.endRendering();
}

// Post-processing -----------------------------------------------------------------------------------

PostProcessingPass::PostProcessingPass(ResourceDeclarations& declarations, bool bloom) :
    m_sceneColor{ declarations.addReference(names::sceneColor, Usage::StorageRead) },
    m_bloom{ bloom ? std::optional{ declarations.addReference(names::bloom, Usage::StorageRead) } : std::nullopt },
    // 16-bit: the image may stay linear until the blit onto an sRGB target encodes it (see execute). As a
    // view's target it is also copied out, for a screenshot.
    m_final{ declarations.addTexture(names::finalImage, Usage::StorageWrite, { .format = rhi::Format::RGBA16_SFLOAT, .usage = rhi::ImageUsage::TransferSrc }) }
{
}

auto PostProcessingPass::name() const -> std::string_view
{
    return "Post Processing";
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
    // An sRGB target (the swapchain prefers one) encodes on write; gamma here would apply it twice. A view's
    // "Final" reaches the same target through the UI, so the same holds for it.
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

PresentPass::PresentPass(ResourceDeclarations& declarations, std::string_view source) :
    m_source{ declarations.addReference(source, Usage::TransferSrc) }
{
}

auto PresentPass::name() const -> std::string_view
{
    return "Present";
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

namespace aegis::render_graph::detail
{
auto makeSkyBoxViewProjection(const FrameScene::Camera& camera) -> glm::mat4
{
    const auto viewRotation = glm::mat4{ glm::mat3{ camera.view } };
    return glm::rowMajor4(camera.projection * viewRotation);
}
}
