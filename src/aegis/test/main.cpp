#include <algorithm>
#include <array>
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
class Engine
{
public:
    static auto create() -> std::expected<Engine, std::string>
    {
        Engine engine{};

        platform::Window::Desc windowDesc{
            .title = "Test Window",
            .width = 640,
            .height = 480,
        };
        engine.m_window = std::make_unique<platform::Window>(windowDesc);

        auto context = rhi::Context::create({
            .appName = "TestApp",
            .window = *engine.m_window,
        });
        if (!context)
            return std::unexpected{ "Failed to create rhi context" };
        engine.m_context = std::move(*context);

        auto device = engine.m_context->createDevice({});
        if (!device)
            return std::unexpected{ "Failed to create rhi device" };
        engine.m_device = std::move(*device);

        auto renderer = renderer::Renderer::create({
            .window = *engine.m_window,
            .context = *engine.m_context,
            .device = *engine.m_device,
        });
        if (!renderer)
            return std::unexpected{ "Failed to create renderer" };
        engine.m_renderer = std::move(*renderer);

        auto shader = loadSPIRV(SHADER_PATH);
        if (!shader)
            return std::unexpected{ std::format("Failed to load shader from {}", SHADER_PATH) };

        auto colorAttachments = std::array{ engine.m_renderer->swapchain().surfaceFormat() };
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
            .extent = rhi::Extent3D{ engine.m_renderer->swapchain().extent() },
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

        return engine;
    }

    // Smoke-tests Device::upload / flushUploads against a real device. Unit tests cover the offset
    // math; only a round-trip proves the recorded copies actually land where that math says.
    auto verifyUploads() -> std::expected<void, std::string>
    {
        if (auto result = verifyBufferUpload(); !result)
            return result;
        if (auto result = verifyImageUpload(); !result)
            return result;
        if (auto result = verifyCubemapUpload(); !result)
            return result;
        return verifySinglePixelUploads();
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

    auto run() -> int
    {
        while (!m_window->shouldClose())
        {
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

            // Nothing queues uploads per frame yet; drain anyway so the path stays exercised.
            std::ignore = m_device->flushUploads();

            m_renderer->renderFrame([this](const auto& frameInfo) { drawFrame(frameInfo); });
        }

        m_device->waitIdle();

        const auto frameTimesResult = reportFrameTimes();
        if (const auto errors = rhi::Context::validationErrorCount(); errors > 0)
        {
            std::println("Validation failed: {} errors reported (see stderr)", errors);
            return 1;
        }
        return frameTimesResult;
    }

    auto drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void
    {
        const auto& [cmd, swapchainImage, frameIndex, extent] = frameInfo;

        // beginFrame waited for the frame that last used this slot, so its timestamps are final.
        auto& timer = m_frameTimers[frameIndex];
        if (timer.pending)
            recordFrameTime(timer);

        cmd.begin();
        cmd.resetQueries(timer.pool, 0, 2);
        cmd.beginLabel("Frame");
        cmd.transitionImageLayout(swapchainImage,
            rhi::ResourceState::Unknown,
            rhi::ResourceState::ColorAttachment);
        cmd.transitionImageLayout(
            m_depthTexture.view,
            rhi::ResourceState::Unknown,
            rhi::ResourceState::DepthWrite);

        cmd.beginLabel("Rendering");
        cmd.writeTimestamp(timer.pool, 0, rhi::TimestampStage::TopOfPipe);

        std::array colorAttachments{
            rhi::Attachment::color(
                swapchainImage,
                rhi::ClearColor{ 0.0, 0.0, 0.0, 0.0 }
            )
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

        cmd.transitionImageLayout(swapchainImage,
            rhi::ResourceState::ColorAttachment,
            rhi::ResourceState::Present);

        cmd.endLabel();
        cmd.end();
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

    std::unique_ptr<platform::Window> m_window;
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

auto main() -> int
{
    auto engine = aegis::Engine::create();
    if (!engine)
    {
        std::println("Failed to create engine \n{}", engine.error());
        return 1;
    }
    return engine->run();
}
