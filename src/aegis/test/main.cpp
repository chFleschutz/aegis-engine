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

            m_device->flushUploads();

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
