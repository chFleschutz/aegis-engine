module aegis.test;

import std;
import aegis.render_graph;
import aegis.render_passes;
import aegis.rhi;

namespace aegis::test
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

namespace
{
// The pool's view of each mip of the probe texture covers that mip alone, at its extent, with the descriptors
// its usage allows (sampled, not storage).
auto checkMipViews(const rhi::Device& device, const render_graph::ResourcePool& pool, render_graph::ResourceId texture)
    -> std::expected<void, std::string>
{
    const auto& image = device.get(pool.texture(texture).image);
    for (std::uint32_t mip = 0; mip < image.mipLevels(); ++mip)
    {
        const auto& view = device.get(pool.mipView(texture, mip));
        const rhi::Extent3D expected{ std::max(image.extent().x >> mip, 1u), std::max(image.extent().y >> mip, 1u), 1 };
        if (view.range().baseMipLevel != mip || view.range().mipLevelCount != 1 || view.extent() != expected)
            return std::unexpected{ std::format("Mip view {} does not cover that mip alone", mip) };
        if (!view.sampledDescriptor() || view.storageDescriptor())
            return std::unexpected{ std::format("Mip view {} lacks the descriptors of the texture's usage", mip) };
    }
    return {};
}
}

// A graph texture that follows the output: its view covers all mips and keeps its bindless slot when
// FrameGraph::resized recreates it at the new extent, and the graph frees it again. So do its mip views.
auto Harness::verifyGraphTextures() -> std::expected<void, std::string>
{
    const auto liveBefore = m_device->liveResources();
    {
        render_graph::GraphBuilder builder;
        const auto& probe = builder.add<OutputProbePass>();
        auto created = render_graph::FrameGraph::create(*m_device, std::move(builder), m_renderer->extent());
        if (!created)
            return std::unexpected{ std::format("Compiling the probe graph: {}", describe(created.error())) };
        auto& graph = **created;

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
        if (auto views = checkMipViews(*m_device, graph.pool(), probe.texture()); !views)
            return views;
        std::vector<rhi::ImageViewHandle> mipViews;
        for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
            mipViews.push_back(graph.pool().mipView(probe.texture(), mip));
        const auto mipSlot = *m_device->get(mipViews[1]).sampledDescriptor();

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
        if (auto views = checkMipViews(*m_device, graph.pool(), probe.texture()); !views)
            return std::unexpected{ std::format("After a resize: {}", views.error()) };
        // The new extent may have a mip fewer; the views of the mips both images have keep their handles.
        for (std::uint32_t mip = 0; mip < std::min(mipLevels, m_device->get(texture.image).mipLevels()); ++mip)
        {
            if (graph.pool().mipView(probe.texture(), mip) != mipViews[mip])
                return std::unexpected{ std::format("Mip view {} changed its handle across a resize", mip) };
        }
        if (m_device->get(mipViews[1]).sampledDescriptor()->index != mipSlot.index)
            return std::unexpected{ "A mip view lost its bindless slot across a resize" };

        // A tiny output clamps the chain to one mip: the views of the others go, the first one stays.
        m_device->waitIdle();
        if (auto resized = graph.resized({ 2, 2 }); !resized)
            return std::unexpected{ std::format("Resizing the probe graph to a tiny extent: {}", describe(resized.error())) };
        if (m_device->get(texture.image).mipLevels() != 1 || graph.pool().mipView(probe.texture(), 0) != mipViews[0])
            return std::unexpected{ "A tiny resize did not keep the first mip view alone" };
        if (auto views = checkMipViews(*m_device, graph.pool(), probe.texture()); !views)
            return std::unexpected{ std::format("After a tiny resize: {}", views.error()) };

        // And back: the chain grows again, with new views of the mips the tiny image lacked.
        m_device->waitIdle();
        if (auto resized = graph.resized(resizedTo); !resized)
            return std::unexpected{ std::format("Resizing the probe graph back from a tiny extent: {}", describe(resized.error())) };
        if (m_device->get(texture.image).mipLevels() <= 1 || graph.pool().mipView(probe.texture(), 0) != mipViews[0])
            return std::unexpected{ "Growing from a tiny extent did not keep the first mip view" };
        if (auto views = checkMipViews(*m_device, graph.pool(), probe.texture()); !views)
            return std::unexpected{ std::format("After growing from a tiny extent: {}", views.error()) };

        std::println("Graph textures verified: {} mips, bindless slot {} kept across a resize", mipLevels, after->index);
    }
    if (m_device->liveResources() != liveBefore)
        return std::unexpected{ "The probe graph leaked resources when destroyed" };
    return {};
}

// Adds one to every texel each frame, twice: from the previous frame's image of a history pair into the current
// one, and in place in a persistent texture of a fixed size (which the check copies from, hence TransferSrc).
// Starts over at one when the texture is fresh.
class AccumulatePass : public render_graph::RenderPass
{
public:
    static constexpr rhi::Extent2D totalExtent{ 16, 16 };

    explicit AccumulatePass(render_graph::ResourceDeclarations& declarations) :
        m_history{ declarations.addTexture("History", render_graph::Usage::StorageWrite,
            { .format = rhi::Format::RGBA16_SFLOAT, .lifetime = render_graph::Lifetime::History }) },
        m_previous{ declarations.addHistoryReference("History", render_graph::Usage::StorageRead) },
        m_total{ declarations.addTexture("Total", render_graph::Usage::StorageWrite,
            { .format = rhi::Format::RGBA16_SFLOAT, .extent = totalExtent, .usage = rhi::ImageUsage::TransferSrc, .lifetime = render_graph::Lifetime::Persistent }) }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Accumulate"; }

    [[nodiscard]] auto createResources(render_graph::ResourcePool& pool) -> std::expected<void, render_graph::Error> override
    {
        auto shader = loadSPIRV(AEGIS_TEST_ACCUMULATE_SHADER_PATH);
        if (!shader)
            return render_graph::makeError(render_graph::ErrorCode::ResourceCreationFailed, shader.error());
        auto pipeline = pool.device().createPipeline(rhi::Pipeline::ComputeDesc{
            .name = "AccumulatePipeline",
            .shader = {
                .name = "AccumulateShader",
                .stage = rhi::ShaderStage::Compute,
                .code = *shader,
                .entryPoint = "accumulateMain",
            },
        });
        if (!pipeline)
            return render_graph::makeError(render_graph::ErrorCode::ResourceCreationFailed, "Failed to create the accumulate pipeline", pipeline.error());
        m_pipeline = std::move(*pipeline);
        return {};
    }

    auto execute(render_graph::ResourcePool& pool, const render_graph::FrameInfo& frameInfo) -> void override
    {
        // Matches AccumulateConstants in accumulate.slang.
        struct AccumulateConstants
        {
            rhi::StorageImageDescriptor input;
            rhi::StorageImageDescriptor output;
            std::uint32_t fresh;
        };
        constexpr std::uint32_t groupSize{ 8 };
        const auto accumulate = [&](render_graph::ResourceId input, render_graph::ResourceId output) {
            const auto extent = pool.extent(output);
            frameInfo.cmd.pushConstants(AccumulateConstants{
                .input = pool.storageDescriptor(input),
                .output = pool.storageDescriptor(output),
                .fresh = pool.isFresh(output) ? 1u : 0u,
            });
            frameInfo.cmd.dispatch((extent.x + groupSize - 1) / groupSize, (extent.y + groupSize - 1) / groupSize);
        };

        frameInfo.cmd.bindPipeline(*m_pipeline);
        // The pair's images swap every frame: both are looked up now, not once.
        accumulate(m_previous, m_history);
        accumulate(m_total, m_total);
    }

    [[nodiscard]] auto history() const noexcept -> render_graph::ResourceId { return m_history; }
    [[nodiscard]] auto previous() const noexcept -> render_graph::ResourceId { return m_previous; }
    [[nodiscard]] auto total() const noexcept -> render_graph::ResourceId { return m_total; }

private:
    render_graph::ResourceId m_history;
    render_graph::ResourceId m_previous;
    render_graph::ResourceId m_total;
    std::optional<rhi::Pipeline> m_pipeline;
};

// Copies the history pair's current image into a texture the check owns, imported into the graph with no
// contents and left ready to be copied from.
class ExportPass : public render_graph::RenderPass
{
public:
    explicit ExportPass(render_graph::ResourceDeclarations& declarations) :
        m_source{ declarations.addReference("History", render_graph::Usage::TransferSrc) },
        m_export{ declarations.importTexture("Export", render_graph::Usage::TransferDst,
            { .format = rhi::Format::RGBA16_SFLOAT, .after = render_graph::Usage::TransferSrc }) }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Export"; }

    auto execute(render_graph::ResourcePool& pool, const render_graph::FrameInfo& frameInfo) -> void override
    {
        frameInfo.cmd.blit(pool.texture(m_source).view, pool.texture(m_export).view, rhi::Filter::Nearest);
    }

    [[nodiscard]] auto exported() const noexcept -> render_graph::ResourceId { return m_export; }

private:
    render_graph::ResourceId m_source;
    render_graph::ResourceId m_export;
};

// Graph resources that outlive a frame, on a device with synchronization validation: a history pair counts the
// frames by adding one to the previous frame's image, a persistent texture by adding one in place, and an
// imported texture carries the result out. A resize recreates the pair, which starts over; the persistent
// texture has a fixed size and counts on.
auto Harness::verifyGraphHistory() -> std::expected<void, std::string>
{
    constexpr std::uint32_t size{ AccumulatePass::totalExtent.x };
    constexpr std::uint32_t framesBefore{ 5 };
    constexpr std::uint32_t framesAfter{ 3 };

    const auto liveBefore = m_device->liveResources();
    auto exported = renderer::createTexture(*m_device, {
                                                           .name = "GraphHistoryExport",
                                                           .extent = { size, size, 1 },
                                                           .format = rhi::Format::RGBA16_SFLOAT,
                                                           .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc | rhi::ImageUsage::Sampled, // Sampled: its view needs a usage
                                                       });
    if (!exported)
        return std::unexpected{ "Failed to create the export texture" };

    // One red value of every texel of a 'size' square image that is in 'state', or an error if they differ.
    const auto valueOf = [&](rhi::ImageHandle image, rhi::ResourceState state) -> std::expected<float, std::string> {
        const auto texels = std::size_t{ size } * size;
        auto readback = m_device->createBuffer({
            .name = "GraphHistoryReadback",
            .size = texels * 8,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ "Failed to create the history readback buffer" };
        const std::array regions{ rhi::BufferImageCopy{ .extent = { size, size, 1 } } };
        auto copied = submitAndWait("GraphHistoryReadback", [&](const auto& cmd) {
            cmd.transitionImageLayout(image, state, rhi::ResourceState::CopySrc);
            cmd.copyImageToBuffer(image, m_device->get(*readback), regions);
            cmd.transitionImageLayout(image, rhi::ResourceState::CopySrc, state);
        });
        std::vector<std::uint16_t> halves(texels * 4);
        if (copied)
            m_device->get(*readback).read(reinterpret_cast<std::byte*>(halves.data()), halves.size() * 2);
        m_device->free(*readback);
        if (!copied)
            return std::unexpected{ copied.error() };

        const auto first = halfToFloat(halves[0]);
        for (std::size_t texel = 0; texel < texels; ++texel)
        {
            if (const auto value = halfToFloat(halves[texel * 4]); value != first)
                return std::unexpected{ std::format("texel {} holds {}, the first {}", texel, value, first) };
        }
        return first;
    };

    std::expected<void, std::string> result;
    {
        render_graph::GraphBuilder builder;
        const auto& accumulate = builder.add<AccumulatePass>();
        const auto& exporting = builder.add<ExportPass>();
        auto created = render_graph::FrameGraph::create(*m_device, std::move(builder), { size, size });
        if (!created)
        {
            renderer::freeTexture(*m_device, *exported);
            return std::unexpected{ std::format("Compiling the history graph: {}", describe(created.error())) };
        }
        auto& graph = **created;
        graph.bind(exporting.exported(), *exported);

        std::uint32_t frame{ 0 };
        const auto run = [&](std::uint32_t frames) -> std::expected<void, std::string> {
            for (std::uint32_t i = 0; i < frames; ++i, ++frame)
            {
                auto ran = submitAndWait("GraphHistoryFrame", [&](const auto& cmd) {
                    // The graph declares no frame target: nothing to bind
                    graph.execute(cmd, frame % renderer::Renderer::maxFramesInFlight, {});
                });
                if (!ran)
                    return ran;
            }
            return {};
        };
        // The pair through the imported texture, which the graph leaves ready to copy from; the persistent
        // texture where the pass left it.
        const auto expect = [&](std::string_view when, float history, float total) -> std::expected<void, std::string> {
            const auto historyValue = valueOf(exported->image, rhi::ResourceState::CopySrc);
            const auto totalValue = valueOf(graph.pool().texture(accumulate.total()).image, rhi::ResourceState::ComputeStorage);
            if (!historyValue || !totalValue)
                return std::unexpected{ std::format("{}: {}", when, !historyValue ? historyValue.error() : totalValue.error()) };
            if (*historyValue != history || *totalValue != total)
            {
                return std::unexpected{ std::format("{}: the history pair holds {} (want {}), the persistent texture {} (want {})",
                    when, *historyValue, history, *totalValue, total) };
            }
            return {};
        };

        result = [&]() -> std::expected<void, std::string> {
            const auto& pool = graph.pool();
            if (!pool.isFresh(accumulate.history()) || !pool.isFresh(accumulate.total()))
                return std::unexpected{ "A history pair or persistent texture is not fresh before the first frame" };
            if (pool.texture(accumulate.history()).image == pool.texture(accumulate.previous()).image)
                return std::unexpected{ "A history pair's current and previous image are the same" };
            const auto firstCurrent = pool.texture(accumulate.history()).image;

            if (auto ran = run(framesBefore); !ran)
                return ran;
            if (pool.isFresh(accumulate.history()) || pool.isFresh(accumulate.total()))
                return std::unexpected{ "A history pair or persistent texture is still fresh after a frame" };
            // An odd number of frames: the pair has swapped.
            if (pool.texture(accumulate.previous()).image != firstCurrent)
                return std::unexpected{ "The history pair did not swap its images" };
            if (auto matched = expect(std::format("After {} frames", framesBefore), framesBefore, framesBefore); !matched)
                return matched;

            m_device->waitIdle();
            if (auto resized = graph.resized({ size / 2, size / 2 }); !resized)
                return std::unexpected{ std::format("Resizing the history graph: {}", describe(resized.error())) };
            if (!pool.isFresh(accumulate.history()))
                return std::unexpected{ "A resize did not make the history pair fresh" };
            if (pool.isFresh(accumulate.total()))
                return std::unexpected{ "A resize made a persistent texture of a fixed size fresh" };

            if (auto ran = run(framesAfter); !ran)
                return ran;
            return expect(std::format("{} frames after a resize", framesAfter), framesAfter, framesBefore + framesAfter);
        }();
        m_device->waitIdle();
    }
    renderer::freeTexture(*m_device, *exported);
    if (!result)
        return result;
    if (m_device->liveResources() != liveBefore)
        return std::unexpected{ "The history graph leaked resources when destroyed" };

    std::println("Graph history verified: a history pair counted {} frames and {} after a resize, a persistent texture {}",
        framesBefore, framesAfter, framesBefore + framesAfter);
    return {};
}

// Builds, destroys and rebuilds the default graph: the graph and its passes free everything they
// create, so the number of live device resources is the same each time. Then resizes a rebuilt graph.
auto Harness::verifyGraphRebuild(bool gpuDriven) -> std::expected<void, std::string>
{
    // The graphs never record a frame, so nothing is ever bound to it.
    const render_graph::FrameSceneSource scene;
    // Outlives the graphs, as the engine's does. Its BRDF LUT is computed before counting: it stays after the graphs.
    render_graph::PipelineLibrary pipelines;
    render_graph::RenderSettings settings;
    if (auto lut = pipelines.brdfLut(*m_device); !lut)
        return std::unexpected{ std::format("Computing the BRDF LUT: {}", describe(lut.error())) };
    const auto build = [&](std::unique_ptr<render_graph::FrameGraph>& graph) -> std::expected<void, std::string> {
        render_graph::GraphBuilder builder;
        render_graph::addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = gpuDriven, .targetFormat = m_renderer->targetFormat() });
        auto created = render_graph::FrameGraph::create(*m_device, std::move(builder), m_renderer->extent());
        if (!created)
            return std::unexpected{ std::format("Compiling the default graph: {}", describe(created.error())) };
        graph = std::move(*created);
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

namespace aegis::test
{
// Compiles the default graph with one main view, then with two (each with its shadow views), then rebuilds the
// two-view graph, all with one pipeline library: the second view's passes and the rebuilt graph's find every
// pipeline and the BRDF LUT in it, so they create none and submit no IBL work. The graphs record no frame, so this
// covers the pipelines passes take when compiled; the draw batches' pipelines, taken in prepare, are covered by the
// --views scene run.
auto Harness::verifyPipelineSharing(bool gpuDriven) -> std::expected<void, std::string>
{
    const render_graph::FrameSceneSource scene; // Never bound: no frame is recorded
    render_graph::PipelineLibrary pipelines;
    render_graph::RenderSettings settings;
    const auto compile = [&](std::vector<render_graph::DefaultPassesDesc::View> views) -> std::expected<void, std::string> {
        render_graph::addShadowViews(views);
        render_graph::GraphBuilder builder;
        render_graph::addDefaultPasses(builder, { .scene = scene, .pipelines = pipelines, .settings = settings, .gpuDriven = gpuDriven, .targetFormat = m_renderer->targetFormat(), .views = std::move(views) });
        auto created = render_graph::FrameGraph::create(*m_device, std::move(builder), m_renderer->extent());
        if (!created)
            return std::unexpected{ std::format("Compiling the default graph: {}", describe(created.error())) };
        m_device->waitIdle();
        return {};
    };

    if (auto compiled = compile({ { .name = "Main" } }); !compiled)
        return compiled;
    const auto oneView = pipelines.size();
    const auto firstLut = pipelines.computedBrdfLut();
    if (oneView == 0 || !firstLut)
        return std::unexpected{ "The default graph took no pipeline or no BRDF LUT from its library" };

    for (const auto* build : { "with a second view", "rebuilt" })
    {
        if (auto compiled = compile({ { .name = "Edit" }, { .name = "Play", .extent = rhi::Extent2D{ 320, 240 } } }); !compiled)
            return compiled;
        if (pipelines.size() != oneView)
            return std::unexpected{ std::format("The default graph {} holds {} pipelines in its library, with one view {}", build, pipelines.size(), oneView) };
        if (pipelines.computedBrdfLut() != firstLut)
            return std::unexpected{ std::format("The default graph {} computed the BRDF LUT again", build) };
    }

    std::println("Pipeline sharing verified ({}): {} pipelines and one BRDF LUT for one view, two and a rebuild",
        gpuDriven ? "GPU-driven" : "CPU-driven", oneView);
    return {};
}
}
