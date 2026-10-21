module aegis.test;

import std;
import aegis.platform;
import aegis.renderer;
import aegis.render_graph;
import aegis.rhi;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;

auto loadSPIRV(const std::filesystem::path& path) -> std::expected<SpirvBuffer, std::string>
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open())
        return std::unexpected{ std::format("Failed to open shader: {}", path.string()) };

    const auto fileSize = static_cast<std::size_t>(file.tellg());
    if (fileSize % sizeof(std::uint32_t) != 0)
        return std::unexpected{ "SPIR-V file size is not a multiple of 4 bytes" };

    SpirvBuffer buffer(fileSize / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));

    if (!file)
        return std::unexpected{ "Failed to read complete shader file" };

    return buffer;
}

namespace aegis
{
auto Engine::create(const Options& options) -> std::expected<Engine, std::string>
{
    Engine engine{};
    engine.m_options = options;

    if (!options.headless)
    {
        platform::Window::Desc windowDesc{
            .title = "Test Window",
            .width = 640,
            .height = 480,
        };
        auto window = platform::Window::create(windowDesc);
        if (!window)
            return std::unexpected{ std::format("Failed to create window: {}", platform::describe(window.error())) };
        engine.m_window = std::make_unique<platform::Window>(std::move(*window));
        engine.m_input = std::make_unique<platform::Input>(*engine.m_window);

        if (auto result = engine.verifyInput(); !result)
            return std::unexpected{ result.error() };
    }

    auto context = rhi::Context::create({
        .appName = "TestApp",
        .window = engine.m_window.get(),
    });
    if (!context)
        return std::unexpected{ std::format("Failed to create rhi context: {}", rhi::describe(context.error())) };
    engine.m_context = std::move(*context);

    auto device = engine.m_context->createDevice({});
    if (!device)
        return std::unexpected{ std::format("Failed to create rhi device: {}", rhi::describe(device.error())) };
    if (options.gpuDriven && !(*device)->capabilities().meshShaders)
        return std::unexpected{ std::string{ skipMeshShaders } };
    engine.m_device = std::move(*device);

    auto renderer = renderer::Renderer::create({
        .context = *engine.m_context,
        .device = *engine.m_device,
        .window = engine.m_window.get(),
        .offscreen = { .extent = { 640, 480 } },
    });
    if (!renderer)
        return std::unexpected{ std::format("Failed to create renderer: {}", renderer::describe(renderer.error())) };
    engine.m_renderer = std::move(*renderer);

    auto shader = loadSPIRV(AEGIS_TEST_SHADER_PATH);
    if (!shader)
        return std::unexpected{ std::format("Failed to load shader from {}", AEGIS_TEST_SHADER_PATH) };

    auto colorAttachments = std::array{ rhi::Pipeline::ColorAttachment{ engine.m_renderer->targetFormat() } };
    auto shaders = std::array{
        rhi::Pipeline::Shader{
            .name = "TriangleVertexShader",
            .stage = rhi::ShaderStage::Vertex,
            .code = *shader,
            .entryPoint = "vertexMain",
        },
        rhi::Pipeline::Shader{
            .name = "TriangleFragmentShader",
            .stage = rhi::ShaderStage::Fragment,
            .code = *shader,
            .entryPoint = "fragmentMain",
        },
    };
    rhi::Pipeline::GraphicsDesc pipelineDesc{
        .name = "TrianglePipeline",
        .shaders = shaders,
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Format::D32_SFLOAT,
    };
    auto pipeline = engine.m_device->createPipeline(pipelineDesc);
    if (!pipeline)
        return std::unexpected{ "Failed to create pipeline" };
    engine.m_pipeline = std::move(*pipeline);

    // Every fixed-function field away from its default, drawn each frame so validation checks the
    // state against the render pass at draw time, not only at creation. The triangle is
    // counter-clockwise, so with a clockwise front face it is back-facing and survives front culling.
    const auto overlayAttachments = std::array{
        rhi::Pipeline::ColorAttachment{ engine.m_renderer->targetFormat(), rhi::BlendMode::Alpha },
    };
    auto overlay = engine.m_device->createPipeline(rhi::Pipeline::GraphicsDesc{
        .name = "TriangleOverlayPipeline",
        .shaders = shaders,
        .colorAttachments = overlayAttachments,
        .depthAttachment = rhi::Format::D32_SFLOAT,
        .depth = { .test = true, .write = false, .compareOp = rhi::CompareOp::LessOrEqual },
        .cullMode = rhi::CullMode::Front,
        .frontFace = rhi::FrontFace::Clockwise,
    });
    if (!overlay)
        return std::unexpected{ "Failed to create overlay pipeline" };
    engine.m_overlayPipeline = std::move(*overlay);

    auto depthTexture = renderer::createTexture(*engine.m_device, engine.depthDesc());
    if (!depthTexture)
        return std::unexpected{ "Failed to create depth image" };
    engine.m_depthTexture = std::move(*depthTexture);

    for (std::uint32_t i = 0; i < renderer::Renderer::maxFramesInFlight; ++i)
    {
        auto pool = engine.m_device->createQueryPool({ .name = "FrameTimestamps", .count = 2 });
        if (!pool)
            return std::unexpected{ "Failed to create timestamp query pool" };
        engine.m_frameTimers.push_back({ .pool = std::move(*pool) });
    }

    if (auto result = engine.verifyUploads(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifySamplers(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifyBufferDeviceAddress(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifyComputeRoundTrip(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifyGraphTextures(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifyGraphRebuild(false); !result)
        return std::unexpected{ result.error() };
    if (engine.m_device->capabilities().meshShaders)
    {
        if (auto result = engine.verifyGraphRebuild(true); !result)
            return std::unexpected{ result.error() };
    }
    if (auto result = engine.verifyAssets(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifyAssetLibrary(); !result)
        return std::unexpected{ result.error() };
    if (auto result = engine.verifyIbl(); !result)
        return std::unexpected{ result.error() };

    if (options.scene)
    {
        if (auto result = engine.createScene(); !result)
            return std::unexpected{ result.error() };
    }

    return engine;
}

auto Engine::run() -> int
{
    std::uint32_t frames{ 0 };
    while (m_options.frames == 0 || frames < m_options.frames)
    {
        if (m_window)
        {
            if (m_window->shouldClose())
                break;

            m_window->update();
            if (m_window->isMinimized())
            {
                m_window->waitEvents();
                continue;
            }
            m_input->update(); // Drains the window's events

            if (m_window->wasResized() || m_renderer->needsResize())
            {
                if (auto resized = m_renderer->resize(rhi::Extent2D{ m_window->extent() }); !resized)
                {
                    std::println("Renderer resize failed: {}", renderer::describe(resized.error()));
                    m_device->waitIdle();
                    return 1;
                }
                m_window->resetResized();
                if (auto recreated = renderer::recreateTexture(*m_device, m_depthTexture, depthDesc()); !recreated)
                {
                    std::println("Depth resize failed");
                    return 1;
                }
                if (m_frameGraph)
                {
                    if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
                        std::println("Frame graph resize failed: {}", describe(resized.error()));
                }
            }
        }

        // Nothing queues uploads per frame yet; drain anyway so the path stays exercised.
        if (auto flushed = m_device->flushUploads(); !flushed)
        {
            std::println("Flushing uploads failed: {}", rhi::describe(flushed.error()));
            m_device->waitIdle();
            return 1;
        }

        if (m_scene)
        {
            // The projection is built from the camera's aspect: take it from the frame, as the Engine does.
            const auto extent = m_renderer->extent();
            auto& registry = m_scene->registry();
            if (const auto camera = m_scene->mainCamera(); camera && registry.has<aegis::Camera>(camera))
                registry.get<aegis::Camera>(camera).aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
            m_scene->update(1.0f / 60.0f);
        }

        if (m_options.mutateScene)
        {
            if (auto mutated = mutateScene(frames); !mutated)
            {
                std::println("Scene change failed: {}", mutated.error());
                m_device->waitIdle();
                return 1;
            }
        }

        // Headless scene runs resize halfway, so the graph's resolution-dependent textures and the
        // views passes derive from them are recreated under validation.
        if (m_frameGraph && m_renderer->offscreen() && frames == m_options.frames / 2)
        {
            if (auto resized = m_renderer->resize({ 800, 600 }); !resized)
            {
                std::println("Renderer resize failed: {}", renderer::describe(resized.error()));
                return 1;
            }
            if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
            {
                std::println("Frame graph resize failed: {}", describe(resized.error()));
                return 1;
            }
        }

        if (auto rendered = m_renderer->renderFrame([this](const auto& frameInfo) { drawFrame(frameInfo); }); !rendered)
        {
            std::println("Rendering a frame failed: {}", renderer::describe(rendered.error()));
            m_device->waitIdle();
            return 1;
        }
        ++frames;
    }

    m_device->waitIdle();

    auto result = reportFrameTimes();
    if (m_frameGraph)
    {
        // The frame graph's own per-pass timestamps: one entry per pass, read back once complete.
        const auto timings = m_frameGraph->timings();
        if (timings.size() != m_frameGraph->passes().size())
        {
            std::println("Pass timings failed: {} timings for {} passes", timings.size(), m_frameGraph->passes().size());
            result = 1;
        }
        for (const auto& [name, gpuMs, cpuMs] : timings)
            std::println("  {:<20} GPU {:7.4f} ms  CPU {:7.4f} ms", name, gpuMs, cpuMs);
    }
    if (m_renderer->offscreen())
    {
        if (auto offscreen = verifyOffscreenFrame(); !offscreen)
        {
            std::println("Offscreen frame check failed: {}", offscreen.error());
            result = 1;
        }
    }
    if (const auto errors = rhi::Context::validationErrorCount(); errors > 0)
    {
        std::println("Validation failed: {} errors reported (see stderr)", errors);
        return 1;
    }
    return result;
}

auto Engine::drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void
{
    const auto& [cmd, target, frameIndex, extent] = frameInfo;

    // beginFrame waited for the frame that last used this slot, so its timestamps are final.
    auto& timer = m_frameTimers[frameIndex];
    if (timer.pending)
        recordFrameTime(timer);

    cmd.resetQueries(timer.pool, 0, 2);
    if (m_frameGraph)
    {
        cmd.writeTimestamp(timer.pool, 0, rhi::TimestampStage::TopOfPipe);
        m_frameGraph->execute({
            .cmd = cmd,
            .scene = *m_scene,
            .drawBatches = *m_drawBatches,
            .frameIndex = frameIndex,
            .extent = extent,
            .target = target,
        });
        cmd.writeTimestamp(timer.pool, 1, rhi::TimestampStage::AllCommands);
        timer.pending = true;
        return;
    }

    cmd.transitionImageLayout(
        m_depthTexture.view,
        rhi::ResourceState::Unknown,
        rhi::ResourceState::DepthWrite);

    cmd.beginLabel("Rendering");
    cmd.writeTimestamp(timer.pool, 0, rhi::TimestampStage::TopOfPipe);

    std::array colorAttachments{
        rhi::Attachment::color(
            target,
            rhi::ClearColor{ 0.0, 0.0, 0.0, 0.0 })
    };
    cmd.beginRendering(rhi::RenderingCmd{
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depth(
            m_depthTexture.view,
            rhi::ClearDepthStencil{ 1.0f, 0 }),
    });
    cmd.bindPipeline(*m_pipeline);
    cmd.setViewport(extent);
    cmd.setScissor(extent);
    cmd.draw(3);
    cmd.bindPipeline(*m_overlayPipeline);
    cmd.draw(3);
    cmd.endRendering();
    cmd.writeTimestamp(timer.pool, 1, rhi::TimestampStage::AllCommands);
    timer.pending = true;

    cmd.endLabel();
}

// The triangle's depth buffer follows the renderer's extent; recreate it after Renderer::resize.
auto Engine::depthDesc() const -> renderer::Texture::Desc
{
    return {
        .name = "SceneDepth",
        .extent = rhi::Extent3D{ m_renderer->extent() },
        .format = rhi::Format::D32_SFLOAT,
        .usage = rhi::ImageUsage::DepthStencilAttachment,
    };
}

auto Engine::recordFrameTime(FrameTimer& timer) -> void
{
    timer.pending = false;
    auto timestamps = timer.pool.readTimestamps(0, 2);
    if (!timestamps)
    {
        ++m_frameTimeErrors;
        return;
    }
    m_frameTimeTotalMs += timer.pool.elapsedMs((*timestamps)[0], (*timestamps)[1]);
    ++m_timedFrames;
}

// The exit criterion for rhi:query: every frame's rendering pass was timed and read back.
auto Engine::reportFrameTimes() const -> int
{
    if (m_frameTimeErrors > 0 || m_timedFrames == 0)
    {
        std::println("Timed pass failed: {} frames read back, {} reads not ready",
            m_timedFrames, m_frameTimeErrors);
        return 1;
    }

    std::println("Timed pass verified: {} frames, {:.4f} ms average GPU time",
        m_timedFrames, m_frameTimeTotalMs / m_timedFrames);
    return 0;
}

// Drains the open upload batch and blocks until the GPU has consumed it.
auto Engine::flushAndWait() -> std::expected<void, std::string>
{
    auto flushed = m_device->flushUploads();
    if (!flushed)
        return std::unexpected{ std::format("flushUploads failed: {}", rhi::describe(flushed.error())) };
    if (!*flushed)
        return std::unexpected{ "flushUploads did not submit a batch" };
    m_device->graphicsQueue().wait(**flushed);
    return {};
}
}
