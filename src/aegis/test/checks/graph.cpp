module aegis.test;

import std;
import aegis.render_graph;
import aegis.rhi;

namespace aegis
{
// Declares one texture that follows the output at half its size, with a full mip chain, and one of a fixed
// size; verifyGraphTextures resizes a graph with it.
class OutputProbePass : public render_graph::RenderPass
{
public:
    static constexpr rhi::Extent2D fixedExtent{ 128, 64 };

    explicit OutputProbePass(render_graph::ResourceDeclarations& declarations) :
        m_texture{ declarations.addTexture("ProbeOutput", render_graph::Usage::ColorAttachment,
            { .format = rhi::Format::RGBA8_UNORM, .outputScale = 0.5f, .mipLevels = rhi::Image::fullMipChain, .usage = rhi::ImageUsage::Sampled }) },
        m_fixed{ declarations.addTexture("ProbeFixed", render_graph::Usage::ColorAttachment,
            { .format = rhi::Format::RGBA8_UNORM, .extent = fixedExtent }) }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "OutputProbe"; }
    auto execute(render_graph::ResourcePool&, const render_graph::FrameInfo&) -> void override {}

    [[nodiscard]] auto texture() const noexcept -> render_graph::ResourceId { return m_texture; }
    [[nodiscard]] auto fixedTexture() const noexcept -> render_graph::ResourceId { return m_fixed; }
    [[nodiscard]] static auto scaled(rhi::Extent2D output) -> rhi::Extent2D { return { output.x / 2, output.y / 2 }; }

private:
    render_graph::ResourceId m_texture;
    render_graph::ResourceId m_fixed;
};

// A graph texture that follows the output: its view covers all mips and keeps its bindless slot when
// FrameGraph::resized recreates it at the new extent, and the graph frees it again.
auto Engine::verifyGraphTextures() -> std::expected<void, std::string>
{
    const auto liveBefore = m_device->liveResources();
    {
        render_graph::FrameGraph graph{ *m_device, m_renderer->extent() };
        const auto& probe = graph.add<OutputProbePass>();
        if (auto compiled = graph.compile(); !compiled)
            return std::unexpected{ std::format("Compiling the probe graph: {}", describe(compiled.error())) };

        const auto texture = graph.pool().texture(probe.texture());
        const auto before = m_device->get(texture.view).sampledDescriptor();
        if (!before)
            return std::unexpected{ "Graph texture has no sampled descriptor" };
        const auto extentBefore = graph.pool().extent(probe.texture());
        if (extentBefore != OutputProbePass::scaled(m_renderer->extent()))
            return std::unexpected{ "Graph texture does not have its declared size" };
        const auto mipLevels = m_device->get(texture.image).mipLevels();
        if (m_device->get(texture.view).range().mipLevelCount != mipLevels)
            return std::unexpected{ "Graph texture view does not cover the image's mips" };

        const rhi::Extent2D resizedTo{ extentBefore.x + 64, extentBefore.y + 32 };
        m_device->waitIdle();
        if (auto resized = graph.resized(resizedTo); !resized)
            return std::unexpected{ std::format("Resizing the probe graph: {}", describe(resized.error())) };

        const auto after = m_device->get(texture.view).sampledDescriptor();
        if (!after || after->index != before->index)
            return std::unexpected{ "Graph texture lost its bindless slot across a resize" };
        if (graph.pool().extent(probe.texture()) != OutputProbePass::scaled(resizedTo))
            return std::unexpected{ "Graph texture did not follow the new output extent" };
        if (graph.pool().extent(probe.fixedTexture()) != OutputProbePass::fixedExtent)
            return std::unexpected{ "Fixed-size graph texture changed with the output" };
        if (m_device->get(texture.view).range().mipLevelCount != m_device->get(texture.image).mipLevels())
            return std::unexpected{ "Resized graph texture view does not cover the image's mips" };

        std::println("Graph textures verified: {} mips, bindless slot {} kept across a resize", mipLevels, after->index);
    }
    if (m_device->liveResources() != liveBefore)
        return std::unexpected{ "The probe graph leaked resources when destroyed" };
    return {};
}

// Builds, destroys and rebuilds the default graph: the graph and its passes free everything they
// create, so the number of live device resources is the same each time. Then resizes a rebuilt graph.
auto Engine::verifyGraphRebuild(bool gpuDriven) -> std::expected<void, std::string>
{
    const auto build = [&](std::unique_ptr<render_graph::FrameGraph>& graph) -> std::expected<void, std::string> {
        graph = std::make_unique<render_graph::FrameGraph>(*m_device, m_renderer->extent());
        render_graph::addDefaultPasses(*graph, { .gpuDriven = gpuDriven });
        if (auto compiled = graph->compile(); !compiled)
            return std::unexpected{ std::format("Compiling the default graph: {}", describe(compiled.error())) };
        return {};
    };

    const auto liveBefore = m_device->liveResources();
    std::size_t graphResources{ 0 };
    for (int round = 0; round < 3; ++round)
    {
        std::unique_ptr<render_graph::FrameGraph> graph;
        if (auto built = build(graph); !built)
            return built;
        const auto live = m_device->liveResources();
        if (round == 0)
            graphResources = live.images;
        else if (live.images != graphResources)
            return std::unexpected{ std::format("Rebuild {} holds {} images, the first build {}", round, live.images, graphResources) };

        m_device->waitIdle();
        if (auto resized = graph->resized({ 640, 360 }); !resized)
            return std::unexpected{ std::format("Resizing the default graph: {}", describe(resized.error())) };
        if (m_device->liveResources() != live)
            return std::unexpected{ "Resizing the default graph changed the number of live resources" };

        graph.reset();
        m_device->waitIdle();
        const auto after = m_device->liveResources();
        if (after != liveBefore)
        {
            return std::unexpected{ std::format("Destroying the default graph leaked: {}/{}/{}/{} (buffers/images/views/samplers) live, {}/{}/{}/{} before",
                after.buffers, after.images, after.imageViews, after.samplers,
                liveBefore.buffers, liveBefore.images, liveBefore.imageViews, liveBefore.samplers) };
        }
    }
    std::println("Graph rebuild verified ({}): {} images per graph, nothing leaked over 3 builds",
        gpuDriven ? "GPU-driven" : "CPU-driven", graphResources - liveBefore.images);
    return {};
}
}
