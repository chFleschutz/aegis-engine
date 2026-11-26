module aegis.render_passes;
import std;
import aegis.rhi;

namespace aegis::render_graph
{
ForwardPass::ForwardPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
    std::uint32_t view, const std::optional<LightingPass::ShadowMaps>& shadow) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_view{ view },
    m_sceneColor{ declarations.addReference(names::sceneColor, Usage::ColorAttachment) },
    m_depth{ declarations.addReference(names::depth, Usage::DepthRead) },
    m_cameraData{ declarations.addReference(names::cameraData, Usage::ShaderRead) },
    m_lighting{ declarations.addReference(names::lightingUniforms, Usage::ShaderRead) },
    m_lights{ declarations.addReference(names::lights, Usage::ShaderRead) }
{
    // Declared only so the graph orders and transitions them: the shaders find them through the lighting block.
    if (shadow)
    {
        for (const auto& map : shadow->maps)
            m_shadowMaps.push_back(declarations.addReference(map, Usage::SampledFragment));
        if (!shadow->spotAtlas.empty())
            m_shadowMaps.push_back(declarations.addReference(shadow->spotAtlas, Usage::SampledFragment));
    }
}

auto ForwardPass::name() const -> std::string_view
{
    return "Forward";
}

auto ForwardPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    const std::array colorAttachments{ rhi::Pipeline::ColorAttachment{ .format = pool.format(m_sceneColor) } };
    for (const auto& system : m_renderSystems)
    {
        if (auto created = system->createResources(m_pipelines, pool.device(), { colorAttachments, pool.format(m_depth) }); !created)
            return created;
    }
    return {};
}

auto ForwardPass::prepare(ResourcePool& /*pool*/) -> std::expected<void, Error>
{
    for (const auto& system : m_renderSystems)
    {
        if (auto prepared = system->prepare(m_scene.current()); !prepared)
            return prepared;
    }
    return {};
}

auto ForwardPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // Without an environment the lighting pass wrote no lighting block to shade with (its prepare warned).
    const auto& scene = m_scene.current();
    if (!scene.camera(m_view) || !scene.environment.irradiance || !scene.environment.prefiltered)
        return;

    const auto& cmd = frameInfo.cmd;
    std::array colorAttachments{ rhi::Attachment::colorLoad(pool.texture(m_sceneColor).view) };
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depthReadOnly(pool.texture(m_depth).view),
    });
    cmd.setViewport(pool.extent(m_depth));
    cmd.setScissor(pool.extent(m_depth));

    const RenderContext context{
        .cmd = cmd,
        .frameScene = scene,
        .frameIndex = frameInfo.frameIndex,
        .camera = pool.bufferAddress(m_cameraData),
        .lighting = pool.bufferAddress(m_lighting),
        .view = scene.camera(m_view)->view,
    };
    for (const auto& system : m_renderSystems)
        system->render(context);

    cmd.endRendering();
}
}
