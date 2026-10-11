#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

import aegis.platform.window;
import aegis.renderer;
import aegis.rhi;

using SpirvBuffer = std::vector<uint32_t>;

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
struct Options
{
    bool headless{ false };
    std::uint32_t frames{ 0 }; // 0: until the window is closed
};

auto parseOptions(std::span<char*> args) -> std::expected<Options, std::string>
{
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg{ args[i] };
        if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--frames" && i + 1 < args.size())
        {
            const std::string_view value{ args[++i] };
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.frames);
            if (error != std::errc{} || end != value.data() + value.size() || options.frames == 0)
                return std::unexpected{ std::format("--frames needs a positive integer, got '{}'", value) };
        }
        else
        {
            return std::unexpected{ std::format("Unknown argument '{}'", arg) };
        }
    }

    // Headless has no window to close, so it always stops by itself.
    if (options.headless && options.frames == 0)
        options.frames = 600;
    return options;
}

class Engine
{
public:
    static auto create(const Options& options) -> std::expected<Engine, std::string>
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
            engine.m_window = std::make_unique<platform::Window>(windowDesc);
        }

        auto context = rhi::Context::create({
            .appName = "TestApp",
            .window = engine.m_window.get(),
        });
        if (!context)
            return std::unexpected{ "Failed to create rhi context" };
        engine.m_context = std::move(*context);

        auto device = engine.m_context->createDevice({});
        if (!device)
            return std::unexpected{ "Failed to create rhi device" };
        engine.m_device = std::move(*device);

        auto renderer = renderer::Renderer::create({
            .context = *engine.m_context,
            .device = *engine.m_device,
            .window = engine.m_window.get(),
            .offscreen = { .extent = { 640, 480 } },
        });
        if (!renderer)
            return std::unexpected{ "Failed to create renderer" };
        engine.m_renderer = std::move(*renderer);

        auto shader = loadSPIRV(SHADER_PATH);
        if (!shader)
            return std::unexpected{ std::format("Failed to load shader from {}", SHADER_PATH) };

        auto colorAttachments = std::array{ engine.m_renderer->targetFormat() };
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
            .setLayouts = {},
            .pushConstantRanges = {},
            .shaders = shaders,
            .colorAttachments = colorAttachments,
            .depthAttachment = rhi::Format::D32_SFLOAT,
        };
        auto pipeline = engine.m_device->createPipeline(pipelineDesc);
        if (!pipeline)
            return std::unexpected{ "Failed to create pipeline" };
        engine.m_pipeline = std::move(*pipeline);

        auto depthTexture = engine.m_renderer->createResolutionDependentTexture({
            .name = "SceneDepth",
            .extent = rhi::Extent3D{ engine.m_renderer->extent() },
            .format = rhi::Format::D32_SFLOAT,
            .usage = rhi::ImageUsage::DepthStencilAttachment,
        });
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
        if (auto result = engine.verifyRendererTextures(); !result)
            return std::unexpected{ result.error() };

        return engine;
    }

    // Smoke-tests Device::upload / flushUploads against a real device. Unit tests cover the offset
    // math; only a round-trip proves the recorded copies actually land where that math says.
    auto verifyUploads() -> std::expected<void, std::string>
    {
        if (auto result = verifyBufferUpload(); !result)
            return result;
        if (auto result = verifyOverflowUpload(); !result)
            return result;
        if (auto result = verifyImageUpload(); !result)
            return result;
        if (auto result = verifyCubemapUpload(); !result)
            return result;
        if (auto result = verifySinglePixelUploads(); !result)
            return result;
        return verifyMipGeneration();
    }

    // Mips are generated by linear blits: a blittable format must get them cleanly under validation,
    // and one that cannot be blitted (depth) must be refused up front rather than recorded.
    auto verifyMipGeneration() -> std::expected<void, std::string>
    {
        using namespace rhi::utility;

        constexpr auto extent = rhi::Extent3D{ 4, 4, 1 };
        auto color = m_device->createImage({
            .name = "MipGenTestColor",
            .extent = extent,
            .format = rhi::Format::RGBA8_UNORM,
            .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc | rhi::ImageUsage::Sampled,
            .mipLevels = rhi::Image::fullMipChain,
        });
        auto depth = m_device->createImage({
            .name = "MipGenTestDepth",
            .extent = extent,
            .format = rhi::Format::D32_SFLOAT,
            .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc |
                     rhi::ImageUsage::DepthStencilAttachment,
            .mipLevels = rhi::Image::fullMipChain,
        });
        if (!color || !depth)
            return std::unexpected{ "Failed to create mip generation test images" };

        std::array<std::byte, 4 * 4 * 4> mip0{}; // RGBA8 and D32 are both 4 bytes per texel
        mip0.fill(std::byte{ 0x7F });

        if (!m_device->upload(*color, mip0, true))
            return std::unexpected{ "Mip generation refused a blittable format" };
        auto refused = m_device->upload(*depth, mip0, true);
        if (refused || refused.error().code != rhi::ErrorCode::FormatNotSupported)
            return std::unexpected{ "Mip generation accepted a depth format" };

        if (auto result = flushAndWait(); !result)
            return result;
        const auto mipLevels = m_device->get(*color).mipLevels();
        m_device->free(*color);
        m_device->free(*depth);

        std::println("Mip generation verified: {} mips blitted, depth refused", mipLevels);
        return {};
    }

    // The Desc -> Vulkan mapping returns vk:: types, so no unit test can reach it; validation of these
    // create calls is its only coverage, hence every enum value appears once. The reuse checks pin
    // that a freed bindless slot stays reserved until the GPU retires it, then returns to the heap.
    auto verifySamplers() -> std::expected<void, std::string>
    {
        auto linear = m_device->createSampler({ .name = "SamplerTestLinearRepeat" });
        auto nearest = m_device->createSampler({
            .name = "SamplerTestNearestClampToEdge",
            .magFilter = rhi::Filter::Nearest,
            .minFilter = rhi::Filter::Nearest,
            .mipmapMode = rhi::SamplerMipmapMode::Nearest,
            .addressMode = rhi::SamplerAddressMode::ClampToEdge,
            .maxAnisotropy = 1.0f,
        });
        auto mirrored = m_device->createSampler({
            .name = "SamplerTestMirroredRepeat",
            .addressMode = rhi::SamplerAddressMode::MirroredRepeat,
        });
        auto border = m_device->createSampler({
            .name = "SamplerTestClampToBorder",
            .addressMode = rhi::SamplerAddressMode::ClampToBorder,
            .maxLod = 0.0f,
        });
        if (!linear || !nearest || !mirrored || !border)
            return std::unexpected{ "Failed to create test samplers" };

        const auto freedIndex = m_device->get(*nearest).descriptor().index;
        if (freedIndex == m_device->get(*linear).descriptor().index)
            return std::unexpected{ "Two live samplers share a bindless slot" };

        m_device->free(*nearest);
        auto early = m_device->createSampler({ .name = "SamplerTestBeforeRetire" });
        if (!early)
            return std::unexpected{ "Failed to create sampler after freeing one" };
        if (m_device->get(*early).descriptor().index == freedIndex)
            return std::unexpected{ "A freed sampler slot was reused before the GPU retired it" };

        // Retire the free the way Renderer::beginFrame does: complete a submission and report it.
        if (auto result = submitAndWait("SamplerTestRetire", [](const auto&) {}); !result)
            return result;
        m_device->setFrameCompleted(m_device->graphicsQueue().lastSubmitted());

        auto reused = m_device->createSampler({ .name = "SamplerTestAfterRetire" });
        if (!reused)
            return std::unexpected{ "Failed to create sampler after retiring one" };
        const auto reusedIndex = m_device->get(*reused).descriptor().index;

        m_device->free(*linear);
        m_device->free(*mirrored);
        m_device->free(*border);
        m_device->free(*early);
        m_device->free(*reused);

        if (reusedIndex != freedIndex)
            return std::unexpected{
                std::format("Retired sampler slot {} was not reused (got {})", freedIndex, reusedIndex)
            };

        std::println("Samplers verified: slot {} held until retired, then reused", freedIndex);
        return {};
    }

    // Validation is the real check: it rejects an address query on a buffer missing the usage flag,
    // or on memory allocated without VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT. A shader reading through
    // the address is not covered yet; that needs a push-constant path the RHI does not have.
    auto verifyBufferDeviceAddress() -> std::expected<void, std::string>
    {
        using namespace rhi::utility;

        constexpr rhi::Buffer::Desc desc{
            .name = "DeviceAddressTest",
            .size = 256,
            .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress,
            .memory = rhi::MemoryUsage::GpuOnly,
        };
        auto first = m_device->createBuffer(desc);
        auto second = m_device->createBuffer(desc);
        if (!first || !second)
            return std::unexpected{ "Failed to create device address test buffers" };

        const auto firstAddress = m_device->get(*first).deviceAddress();
        const auto secondAddress = m_device->get(*second).deviceAddress();
        m_device->free(*first);
        m_device->free(*second);

        if (firstAddress == 0 || secondAddress == 0)
            return std::unexpected{ "Buffer device address is null" };
        if (firstAddress == secondAddress)
            return std::unexpected{ "Two live buffers share a device address" };

        std::println("Buffer device addresses verified: {:#x}, {:#x}", firstAddress, secondAddress);
        return {};
    }

    // Renderer textures must reach shaders through bindless, cover all their mips, and keep their
    // bindless slot across a resize. The name is a temporary on purpose: the renderer must copy it.
    auto verifyRendererTextures() -> std::expected<void, std::string>
    {
        using namespace rhi::utility;

        auto texture = m_renderer->createResolutionDependentTexture({
            .name = std::format("RendererTest{}", "Texture"),
            .extent = rhi::Extent3D{ m_renderer->extent() },
            .format = rhi::Format::RGBA8_UNORM,
            .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::ColorAttachment,
            .mipLevels = rhi::Image::fullMipChain,
        });
        if (!texture)
            return std::unexpected{ "Failed to create renderer texture" };

        const auto before = m_device->get(texture->view).sampledDescriptor();
        if (!before)
            return std::unexpected{ "Renderer texture has no sampled descriptor" };
        const auto mipLevels = m_device->get(texture->image).mipLevels();
        if (m_device->get(texture->view).range().mipLevelCount != mipLevels)
            return std::unexpected{ "Renderer texture view does not cover the image's mips" };

        m_renderer->resize(m_renderer->extent());

        const auto after = m_device->get(texture->view).sampledDescriptor();
        if (!after || after->index != before->index)
            return std::unexpected{ "Renderer texture lost its bindless slot across a resize" };
        if (m_device->get(texture->view).range().mipLevelCount != m_device->get(texture->image).mipLevels())
            return std::unexpected{ "Resized renderer texture view does not cover the image's mips" };

        std::println("Renderer textures verified: {} mips, bindless slot {} kept across a resize",
            mipLevels, after->index);
        return {};
    }

    auto run() -> int
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

                if (m_window->wasResized() || m_renderer->needsResize())
                {
                    m_renderer->resize(rhi::Extent2D{ m_window->extent() });
                    m_window->resetResized();
                }
            }

            // Nothing queues uploads per frame yet; drain anyway so the path stays exercised.
            std::ignore = m_device->flushUploads();

            m_renderer->renderFrame([this](const auto& frameInfo) { drawFrame(frameInfo); });
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

    auto drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void
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
                rhi::ClearDepthStencil{ 1.0f, 0 }
            ),
        });
        cmd.bindPipeline(*m_pipeline);
        cmd.setViewport(extent);
        cmd.setScissor(extent);
        cmd.draw(3);
        cmd.endRendering();
        cmd.writeTimestamp(timer.pool, 1, rhi::TimestampStage::AllCommands);
        timer.pending = true;

        cmd.endLabel();
    }

private:
    // One pool per frame in flight, so a frame's timestamps are read only after the GPU finished it.
    struct FrameTimer
    {
        rhi::QueryPool pool;
        bool pending{ false };
    };

    auto recordFrameTime(FrameTimer& timer) -> void
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
    auto reportFrameTimes() const -> int
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

    Engine() = default;

    // The only proof that offscreen frames are drawn at all: read a target back and look for the
    // triangle. Its centre is covered (opaque), a corner only cleared (alpha 0).
    auto verifyOffscreenFrame() -> std::expected<void, std::string>
    {
        const auto& target = m_renderer->offscreenTarget(0);
        const auto extent = m_renderer->extent();
        if (m_renderer->targetFormat() != rhi::Format::RGBA8_UNORM)
            return std::unexpected{ "the check assumes an RGBA8 target" };

        constexpr std::size_t texelSize{ 4 };
        const auto byteSize = std::size_t{ extent.x } * extent.y * texelSize;
        auto readback = m_device->createBuffer({
            .name = "OffscreenReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ "failed to create readback buffer" };

        const std::array regions{ rhi::BufferImageCopy{ .extent = rhi::Extent3D{ extent } } };
        auto copied = submitAndWait("OffscreenReadback", [&](const auto& cmd) {
            cmd.transitionImageLayout(target.view, rhi::ResourceState::ColorAttachment,
                rhi::ResourceState::CopySrc);
            cmd.copyImageToBuffer(target.image, m_device->get(*readback), regions);
        });
        if (!copied)
            return copied;

        std::vector<std::byte> pixels(byteSize);
        m_device->get(*readback).read(pixels.data(), byteSize);
        m_device->free(*readback);

        const auto alphaAt = [&](std::uint32_t x, std::uint32_t y) {
            return std::to_integer<std::uint32_t>(pixels[(std::size_t{ y } * extent.x + x) * texelSize + 3]);
        };
        const auto centre = alphaAt(extent.x / 2, extent.y / 2);
        const auto corner = alphaAt(0, 0);
        if (centre != 255 || corner != 0)
            return std::unexpected{ std::format("centre alpha {} (want 255), corner alpha {} (want 0)", centre, corner) };

        std::println("Offscreen frame verified: {}x{} target read back, triangle drawn", extent.x, extent.y);
        return {};
    }

    // Records 'record' into a one-shot command buffer, submits it, and waits for completion.
    auto submitAndWait(std::string_view name, auto&& record) -> std::expected<void, std::string>
    {
        auto pool = m_device->createCommandPool({
            .name = name,
            .queueFamily = m_device->graphicsQueue().family(),
        });
        if (!pool)
            return std::unexpected{ std::format("{}: failed to create command pool", name) };

        auto cmd = m_device->createCommandBuffer({ .name = name, .pool = *pool });
        if (!cmd)
            return std::unexpected{ std::format("{}: failed to create command buffer", name) };

        cmd->begin(true);
        record(*cmd);
        cmd->end();

        auto timeline = m_device->graphicsQueue().submit(*cmd);
        if (!timeline)
            return std::unexpected{ std::format("{}: failed to submit", name) };
        m_device->graphicsQueue().wait(*timeline);
        return {};
    }

    // Drains the open upload batch and blocks until the GPU has consumed it.
    auto flushAndWait() -> std::expected<void, std::string>
    {
        auto timeline = m_device->flushUploads();
        if (!timeline)
            return std::unexpected{ "flushUploads did not submit a batch" };
        m_device->graphicsQueue().wait(*timeline);
        return {};
    }

    auto verifyBufferUpload() -> std::expected<void, std::string>
    {
        using namespace rhi::utility;

        constexpr std::array<std::uint32_t, 4> srcData{
            0xDEADBEEF, 0x12345678, 0xCAFEBABE, 0x0BADF00D
        };
        constexpr auto byteSize = srcData.size() * sizeof(std::uint32_t);

        auto deviceLocal = m_device->createBuffer({
            .name = "UploadTestDeviceLocal",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst | rhi::BufferUsage::TransferSrc,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        if (!deviceLocal)
            return std::unexpected{ "Failed to create device-local upload target" };

        auto readback = m_device->createBuffer({
            .name = "UploadTestReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ "Failed to create readback buffer" };

        if (!m_device->upload(*deviceLocal, std::as_bytes(std::span{ srcData })))
            return std::unexpected{ "UploadManager could not stage the buffer test data" };
        if (auto result = flushAndWait(); !result)
            return result;

        auto copied = submitAndWait("UploadTestBufferReadback", [&](const auto& cmd) {
            cmd.copyBuffer(m_device->get(*deviceLocal), m_device->get(*readback), byteSize);
        });
        if (!copied)
            return copied;

        std::array<std::uint32_t, 4> dstData{};
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(dstData.data()), byteSize);

        m_device->free(*deviceLocal);
        m_device->free(*readback);

        if (dstData != srcData)
            return std::unexpected{ "Buffer upload readback mismatch" };

        std::println("Buffer upload verified: {} bytes match after round-trip", byteSize);
        return {};
    }

    // Larger than the whole 64 MB staging ring. Before the overflow path this could never be staged,
    // and the documented flush-and-retry response looped forever.
    auto verifyOverflowUpload() -> std::expected<void, std::string>
    {
        using namespace rhi::utility;

        constexpr std::size_t count{ 20 * 1024 * 1024 };
        constexpr auto byteSize = count * sizeof(std::uint32_t);
        std::vector<std::uint32_t> srcData(count);
        for (std::size_t i = 0; i < count; ++i)
            srcData[i] = static_cast<std::uint32_t>(i * 2654435761u);

        auto deviceLocal = m_device->createBuffer({
            .name = "OverflowTestDeviceLocal",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst | rhi::BufferUsage::TransferSrc,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        auto readback = m_device->createBuffer({
            .name = "OverflowTestReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!deviceLocal || !readback)
            return std::unexpected{ "Failed to create overflow test buffers" };

        if (!m_device->upload(*deviceLocal, std::as_bytes(std::span{ srcData })))
            return std::unexpected{ "UploadManager could not stage an upload larger than its ring" };
        if (auto result = flushAndWait(); !result)
            return result;

        auto copied = submitAndWait("OverflowTestReadback", [&](const auto& cmd) {
            cmd.copyBuffer(m_device->get(*deviceLocal), m_device->get(*readback), byteSize);
        });
        if (!copied)
            return copied;

        std::vector<std::uint32_t> dstData(count);
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(dstData.data()), byteSize);

        m_device->free(*deviceLocal);
        m_device->free(*readback);

        if (dstData != srcData)
            return std::unexpected{ "Overflow upload readback mismatch" };

        std::println("Overflow upload verified: {} MB, larger than the staging ring", byteSize >> 20);
        return {};
    }

    // Uploads a mipped 2D texture, giving every mip a distinct fill so that a wrong per-mip offset
    // shows up as a mismatch rather than passing on identical bytes.
    auto verifyImageUpload() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 4, 4, 1 };
        constexpr auto format = rhi::Format::RGBA8_UNORM;
        constexpr std::uint32_t mipLevels{ 3 };
        constexpr std::array<std::byte, mipLevels> mipFill{
            std::byte{ 0xAA }, std::byte{ 0xBB }, std::byte{ 0xCC }
        };

        return verifyTextureRoundTrip("UploadTestImage", rhi::ImageType::Dim2D, extent, format, mipLevels, 1,
            [&](const auto& footprint, std::size_t /*layer*/) { return mipFill[footprint.mipLevel]; });
    }

    // The only case that exercises the array-layer packing contract: layers must sit inside each
    // mip, not after the whole chain. loadDefaultAssets()'s solidColorCube will depend on this.
    auto verifyCubemapUpload() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 2, 2, 1 };
        constexpr auto format = rhi::Format::RGBA8_UNORM;

        return verifyTextureRoundTrip("UploadTestCubemap", rhi::ImageType::Cube, extent, format, 1, 6,
            [](const auto& /*footprint*/, std::size_t layer) {
                return static_cast<std::byte>(0x10 + layer);
            });
    }

    // 1x1 images are what solidColor / solidColorCube produce. Image type used to be inferred from the
    // extent, which made them 1D; the cube case would then fail validation on its cube view.
    auto verifySinglePixelUploads() -> std::expected<void, std::string>
    {
        constexpr auto extent = rhi::Extent3D{ 1, 1, 1 };
        constexpr auto format = rhi::Format::RGBA8_UNORM;

        auto pixel = verifyTextureRoundTrip("UploadTestPixel", rhi::ImageType::Dim2D, extent, format, 1, 1,
            [](const auto& /*footprint*/, std::size_t /*layer*/) { return std::byte{ 0x5A }; });
        if (!pixel)
            return pixel;

        return verifyTextureRoundTrip("UploadTestPixelCube", rhi::ImageType::Cube, extent, format, 1, 6,
            [](const auto& /*footprint*/, std::size_t layer) {
                return static_cast<std::byte>(0x20 + layer);
            });
    }

    // Fills every subresource via 'fillFor', uploads it, reads the image back, and compares. Also
    // creates a default-typed sampled view, so validation checks the view against the image type.
    auto verifyTextureRoundTrip(std::string_view name, rhi::ImageType type, rhi::Extent3D extent,
        rhi::Format format, std::uint32_t mipLevels, std::uint32_t arrayLayers, auto&& fillFor)
        -> std::expected<void, std::string>
    {
        using namespace rhi::utility;

        const auto footprints = rhi::detail::subresourceFootprints(extent, format, mipLevels,
            arrayLayers);
        if (footprints.empty())
            return std::unexpected{ std::format("{}: no footprints for this description", name) };

        const auto& last = footprints.back();
        const auto sourceSize = last.sourceOffset + last.size;
        const auto stagingSize = last.stagingOffset + last.size;

        // Source data is tightly packed, mip-major, layers inside each mip.
        std::vector<std::byte> srcData(sourceSize);
        for (const auto& footprint : footprints)
        {
            const auto layerSize = footprint.size / footprint.arrayLayerCount;
            for (std::uint32_t layer = 0; layer < footprint.arrayLayerCount; ++layer)
            {
                const auto begin = srcData.begin() +
                                   static_cast<std::ptrdiff_t>(footprint.sourceOffset + layer * layerSize);
                std::fill_n(begin, layerSize, fillFor(footprint, layer));
            }
        }

        auto image = m_device->createImage({
            .name = name,
            .type = type,
            .extent = extent,
            .format = format,
            .usage = rhi::ImageUsage::TransferDst | rhi::ImageUsage::TransferSrc |
                     rhi::ImageUsage::Sampled,
            .mipLevels = mipLevels,
            .arrayLayers = arrayLayers,
        });
        if (!image)
            return std::unexpected{ std::format("{}: failed to create image", name) };

        auto view = m_device->createImageView(*image, {
                                                          .name = name,
                                                          .extent = extent,
                                                          .format = format,
                                                          .range = { .mipLevelCount = mipLevels, .arrayLayerCount = arrayLayers },
                                                          .usage = rhi::ImageUsage::Sampled,
                                                      });
        if (!view)
            return std::unexpected{ std::format("{}: failed to create view", name) };
        m_device->free(*view);

        auto readback = m_device->createBuffer({
            .name = "UploadTestImageReadback",
            .size = stagingSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ std::format("{}: failed to create readback buffer", name) };

        if (!m_device->upload(*image, srcData))
            return std::unexpected{ std::format("{}: UploadManager could not stage the image", name) };
        if (auto result = flushAndWait(); !result)
            return result;

        std::vector<rhi::BufferImageCopy> regions;
        regions.reserve(footprints.size());
        for (const auto& footprint : footprints)
        {
            regions.emplace_back(rhi::BufferImageCopy{
                .bufferOffset = footprint.stagingOffset,
                .mipLevel = footprint.mipLevel,
                .baseArrayLayer = 0,
                .arrayLayerCount = footprint.arrayLayerCount,
                .extent = footprint.extent,
            });
        }

        // upload() leaves the image in CopyDst by contract -- the read-state transition is ours.
        auto copied = submitAndWait(name, [&](const auto& cmd) {
            cmd.transitionImageLayout(*image, rhi::ResourceState::CopyDst,
                rhi::ResourceState::CopySrc);
            cmd.copyImageToBuffer(*image, m_device->get(*readback), regions);
        });
        if (!copied)
            return copied;

        std::vector<std::byte> dstData(stagingSize);
        m_device->get(*readback).read(dstData.data(), stagingSize);

        m_device->free(*image);
        m_device->free(*readback);

        for (const auto& footprint : footprints)
        {
            const auto expected = std::span{ srcData }.subspan(footprint.sourceOffset, footprint.size);
            const auto actual = std::span{ dstData }.subspan(footprint.stagingOffset, footprint.size);
            if (!std::ranges::equal(expected, actual))
            {
                return std::unexpected{
                    std::format("{}: mip {} mismatch after round-trip", name, footprint.mipLevel)
                };
            }
        }

        std::println("{} verified: {} mip(s) x {} layer(s), {} bytes match after round-trip", name,
            mipLevels, arrayLayers, sourceSize);
        return {};
    }

    Options m_options;
    std::unique_ptr<platform::Window> m_window; // null when headless
    std::unique_ptr<rhi::Context> m_context;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<renderer::Renderer> m_renderer;

    std::optional<rhi::Pipeline> m_pipeline;
    renderer::Texture m_depthTexture;
    std::vector<FrameTimer> m_frameTimers;
    std::uint32_t m_timedFrames{ 0 };
    std::uint32_t m_frameTimeErrors{ 0 };
    double m_frameTimeTotalMs{ 0.0 };
};
}

auto main(int argc, char** argv) -> int
{
    const auto options = aegis::parseOptions(std::span{ argv, static_cast<std::size_t>(argc) });
    if (!options)
    {
        std::println("{}\nUsage: aegis-test [--headless] [--frames <n>]", options.error());
        return 2;
    }

    auto engine = aegis::Engine::create(*options);
    if (!engine)
    {
        std::println("Failed to create engine \n{}", engine.error());
        return 1;
    }
    return engine->run();
}
