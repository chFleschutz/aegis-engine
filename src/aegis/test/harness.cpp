module aegis.test;

import std;
import aegis.platform;
import aegis.renderer;
import aegis.rhi;

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

namespace aegis::test
{
auto Harness::create(const Options& options) -> std::expected<Harness, std::string>
{
    Harness harness{};
    harness.m_options = options;

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
        harness.m_window = std::make_unique<platform::Window>(std::move(*window));
        harness.m_input = std::make_unique<platform::Input>(*harness.m_window);

        if (auto result = harness.verifyInput(); !result)
            return std::unexpected{ result.error() };
        if (auto result = harness.verifyWindow(); !result)
            return std::unexpected{ result.error() };
    }

    auto context = rhi::Context::create({
        .appName = "TestApp",
        .window = harness.m_window.get(),
    });
    if (!context)
        return std::unexpected{ std::format("Failed to create rhi context: {}", rhi::describe(context.error())) };
    harness.m_context = std::move(*context);

    auto device = harness.m_context->createDevice({});
    if (!device)
        return std::unexpected{ std::format("Failed to create rhi device: {}", rhi::describe(device.error())) };
    harness.m_device = std::move(*device);

    auto renderer = renderer::Renderer::create({
        .context = *harness.m_context,
        .device = *harness.m_device,
        .window = harness.m_window.get(),
        .offscreen = { .extent = { 640, 480 } },
    });
    if (!renderer)
        return std::unexpected{ std::format("Failed to create renderer: {}", renderer::describe(renderer.error())) };
    harness.m_renderer = std::move(*renderer);

    auto shader = loadSPIRV(AEGIS_TEST_SHADER_PATH);
    if (!shader)
        return std::unexpected{ std::format("Failed to load shader from {}", AEGIS_TEST_SHADER_PATH) };

    auto colorAttachments = std::array{ rhi::Pipeline::ColorAttachment{ harness.m_renderer->targetFormat() } };
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
    auto pipeline = harness.m_device->createPipeline(pipelineDesc);
    if (!pipeline)
        return std::unexpected{ "Failed to create pipeline" };
    harness.m_pipeline = std::move(*pipeline);

    // Every fixed-function field away from its default, drawn each frame so validation checks the
    // state against the render pass at draw time, not only at creation. The triangle is
    // counter-clockwise, so with a clockwise front face it is back-facing and survives front culling.
    const auto overlayAttachments = std::array{
        rhi::Pipeline::ColorAttachment{ harness.m_renderer->targetFormat(), rhi::BlendMode::Alpha },
    };
    auto overlay = harness.m_device->createPipeline(rhi::Pipeline::GraphicsDesc{
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
    harness.m_overlayPipeline = std::move(*overlay);

    auto depthTexture = renderer::createTexture(*harness.m_device, harness.depthDesc());
    if (!depthTexture)
        return std::unexpected{ "Failed to create depth image" };
    harness.m_depthTexture = std::move(*depthTexture);

    for (std::uint32_t i = 0; i < renderer::Renderer::maxFramesInFlight; ++i)
    {
        auto pool = harness.m_device->createQueryPool({ .name = "FrameTimestamps", .count = 2 });
        if (!pool)
            return std::unexpected{ "Failed to create timestamp query pool" };
        harness.m_frameTimers.push_back({ .pool = std::move(*pool) });
    }

    if (auto result = harness.verifyUploads(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifySamplers(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyBufferDeviceAddress(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyComputeRoundTrip(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyValidationLog(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyGraphTextures(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyGraphRebuild(false); !result)
        return std::unexpected{ result.error() };
    if (harness.m_device->capabilities().meshShaders)
    {
        if (auto result = harness.verifyGraphRebuild(true); !result)
            return std::unexpected{ result.error() };
    }
    if (options.noSamples)
    {
        std::println("Skipping the asset and IBL checks: they load the sample project");
        return harness;
    }
    if (auto result = harness.verifyAssets(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyAssetLibrary(); !result)
        return std::unexpected{ result.error() };
    if (auto result = harness.verifyIbl(); !result)
        return std::unexpected{ result.error() };
    return harness;
}

auto Harness::run() -> int
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
            }
        }

        // Nothing queues uploads per frame yet; drain anyway so the path stays exercised.
        if (auto flushed = m_device->flushUploads(); !flushed)
        {
            std::println("Flushing uploads failed: {}", rhi::describe(flushed.error()));
            m_device->waitIdle();
            return 1;
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

auto Harness::drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void
{
    const auto& [cmd, target, frameIndex, extent] = frameInfo;

    // beginFrame waited for the frame that last used this slot, so its timestamps are final.
    auto& timer = m_frameTimers[frameIndex];
    if (timer.pending)
        recordFrameTime(timer);

    cmd.resetQueries(timer.pool, 0, 2);
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
auto Harness::depthDesc() const -> renderer::Texture::Desc
{
    return {
        .name = "SceneDepth",
        .extent = rhi::Extent3D{ m_renderer->extent() },
        .format = rhi::Format::D32_SFLOAT,
        .usage = rhi::ImageUsage::DepthStencilAttachment,
    };
}

auto Harness::recordFrameTime(FrameTimer& timer) -> void
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
auto Harness::reportFrameTimes() const -> int
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
auto Harness::flushAndWait() -> std::expected<void, std::string>
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
