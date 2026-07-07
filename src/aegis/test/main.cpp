#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <print>
#include <span>

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

        if (auto result = engine.verifyUpload(); !result)
            return std::unexpected{ result.error() };

        return engine;
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

            m_renderer->renderFrame([this](const auto& frameInfo) { drawFrame(frameInfo); });
        }

        m_device->waitIdle();
        return 0;
    }

    auto drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void
    {
        const auto& [cmd, swapchainImage, frameIndex, extent] = frameInfo;

        cmd.begin();
        cmd.beginLabel("Frame");
        cmd.transitionImageLayout(swapchainImage,
            rhi::ResourceState::Unknown,
            rhi::ResourceState::Attachment);
        cmd.transitionImageLayout(
            m_depthTexture.view,
            rhi::ResourceState::Unknown,
            rhi::ResourceState::Attachment);

        cmd.beginLabel("Rendering");

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

        cmd.endLabel();

        cmd.transitionImageLayout(swapchainImage,
            rhi::ResourceState::Attachment,
            rhi::ResourceState::Present);

        cmd.endLabel();
        cmd.end();
    }

    // Smoke-tests Device::upload / flushUploads: stage known bytes into a device-local buffer,
    // then read them back through a CpuRead buffer to prove the copy actually landed.
    auto verifyUpload() -> std::expected<void, std::string>
    {
        constexpr std::array<std::uint32_t, 4> srcData{
            0xDEADBEEF, 0x12345678, 0xCAFEBABE, 0x0BADF00D
        };
        constexpr auto byteSize = srcData.size() * sizeof(std::uint32_t);

        // TransferDst (upload target) | TransferSrc (readback source). The flag-enum operator| lives
        // in the unexported rhi::utility namespace, so combine the bits explicitly here.
        constexpr auto deviceLocalUsage = static_cast<rhi::BufferUsage>(
            static_cast<std::uint32_t>(rhi::BufferUsage::TransferDst) |
            static_cast<std::uint32_t>(rhi::BufferUsage::TransferSrc));

        auto deviceLocal = m_device->createBuffer({
            .name = "UploadTestDeviceLocal",
            .size = byteSize,
            .usage = deviceLocalUsage,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        if (!deviceLocal)
            return std::unexpected{ "Failed to create device-local upload target" };

        if (!m_device->upload(*deviceLocal, std::as_bytes(std::span{ srcData })))
            return std::unexpected{ "UploadManager could not stage the test data" };

        auto uploadTimeline = m_device->flushUploads();
        if (!uploadTimeline)
            return std::unexpected{ "flushUploads did not submit a batch" };
        m_device->graphicsQueue().wait(*uploadTimeline);

        auto readback = m_device->createBuffer({
            .name = "UploadTestReadback",
            .size = byteSize,
            .usage = rhi::BufferUsage::TransferDst,
            .memory = rhi::MemoryUsage::CpuRead,
        });
        if (!readback)
            return std::unexpected{ "Failed to create readback buffer" };

        auto copyPool = m_device->createCommandPool({
            .name = "UploadTestReadbackPool",
            .queueFamily = m_device->graphicsQueue().family(),
        });
        if (!copyPool)
            return std::unexpected{ "Failed to create readback command pool" };

        auto copyCmd = m_device->createCommandBuffer({
            .name = "UploadTestReadbackCmd",
            .pool = *copyPool,
        });
        if (!copyCmd)
            return std::unexpected{ "Failed to create readback command buffer" };

        copyCmd->begin(true);
        copyCmd->copyBuffer(m_device->get(*deviceLocal), m_device->get(*readback), byteSize);
        copyCmd->end();

        auto copyTimeline = m_device->graphicsQueue().submit(*copyCmd);
        if (!copyTimeline)
            return std::unexpected{ "Failed to submit readback copy" };
        m_device->graphicsQueue().wait(*copyTimeline);

        std::array<std::uint32_t, 4> dstData{};
        m_device->get(*readback).read(reinterpret_cast<std::byte*>(dstData.data()), byteSize);

        m_device->free(*deviceLocal);
        m_device->free(*readback);

        if (dstData != srcData)
            return std::unexpected{ "Upload readback mismatch" };

        std::println("Upload round-trip verified: {} bytes match after UploadManager copy", byteSize);
        return {};
    }

private:
    Engine() = default;

    std::unique_ptr<platform::Window> m_window;
    std::unique_ptr<rhi::Context> m_context;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<renderer::Renderer> m_renderer;

    std::optional<rhi::Pipeline> m_pipeline;
    renderer::Texture m_depthTexture;
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
