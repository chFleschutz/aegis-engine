import aegis.rhi;
import aegis.platform.window;

#include <array>
#include <expected>
#include <filesystem>
#include <fstream>
#include <print>

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

class Engine
{
public:
    static auto create() -> std::expected<Engine, std::string>
    {
        aegis::platform::Window::Desc windowDesc{
            .title = "Test Window",
            .width = 640,
            .height = 480,
        };
        aegis::platform::Window window{ windowDesc };

        auto rhi = aegis::rhi::RHI::create({
            .window = window,
            .appName = "Test",
        });
        if (!rhi)
            return std::unexpected{ "Failed to create RHI" };

        auto shader = loadSPIRV(SHADER_PATH);
        if (!shader)
            return std::unexpected{ std::format("Failed to load shader from {}", SHADER_PATH) };

        auto colorAttachments = std::array{ rhi->swapchain().surfaceFormat() };
        auto shaders = std::array{
            aegis::rhi::Pipeline::Shader{
                .name = "TriangleVertexShader",
                .stage = aegis::rhi::ShaderStage::Vertex,
                .code = *shader,
                .entryPoint = "vertexMain",
            },
            aegis::rhi::Pipeline::Shader{
                .name = "TriangleFragmentShader",
                .stage = aegis::rhi::ShaderStage::Fragment,
                .code = *shader,
                .entryPoint = "fragmentMain",
            },
        };
        aegis::rhi::Pipeline::GraphicsDesc pipelineDesc{
            .name = "TrianglePipeline",
            .setLayouts = {},
            .pushConstantRanges = {},
            .shaders = shaders,
            .colorAttachments = colorAttachments,
            .depthAttachment = aegis::rhi::Format::D32_SFLOAT,
        };
        auto pipeline = device->createPipeline(pipelineDesc);
        if (!pipeline)
            return std::unexpected{ "Failed to create pipeline" };

        aegis::rhi::Image::Desc depthImageDesc{
            .name = "SceneDepth",
            .extent = aegis::rhi::Extent3D{ swapchain->extent() },
            .format = aegis::rhi::Format::D32_SFLOAT,
            .usage = aegis::rhi::ImageUsage::DepthStencilAttachment,
        };
        auto depthImage = device->createImage(depthImageDesc);
        if (!depthImage)
            return std::unexpected{ "Failed to create depth image" };

        aegis::rhi::Buffer::Desc bufferDesc{
            .name = "TestUniformBuffer",
            .size = sizeof(float),
            .usage = aegis::rhi::BufferUsage::Uniform,
        };
        auto buffer = device->createBuffer(bufferDesc);
        if (!buffer)
            return std::unexpected{ "Failed to create buffer" };

        auto uploadPool = device->createCommandPool({
            .name = "Upload Command Pool",
            .queueFamily = device->graphicsQueue().family(),
        });
        if (!uploadPool)
            return std::unexpected{ "Failed to create upload command pool" };

        auto uploadCmd = device->createCommandBuffer({
            .name = "Upload Command Buffer",
            .pool = *uploadPool,
        });
        if (!uploadCmd)
            return std::unexpected{ "Failed to create upload command buffer" };

        return std::expected<Engine, std::string>{
            std::in_place,
            std::move(window),
            std::move(*rhi),
            std::move(*pipeline),
            std::move(*depthImage),
            std::move(*uploadPool),
            std::move(*uploadCmd),
        };
    }

    Engine(aegis::platform::Window&& window,
        aegis::rhi::RHI&& rhi,
        aegis::rhi::Pipeline pipeline,
        aegis::rhi::Image depthImage,
        aegis::rhi::CommandPool uploadPool,
        aegis::rhi::CommandBuffer uploadCmd) :
        m_window{ std::move(window) },
        m_rhi{ std::move(rhi) },
        m_pipeline{ std::move(pipeline) },
        m_depthImage{ std::move(depthImage) },
        m_uploadPool{ std::move(uploadPool) },
        m_uploadCmd{ std::move(uploadCmd) }
    {
    }

    auto run() -> int
    {
        upload();

        while (!m_window.shouldClose())
        {
            m_window.update();
            if (m_window.isMinimized())
            {
                m_window.waitEvents();
                continue;
            }

            if (m_window.wasResized() || m_swapchain.needsRecreation())
                resize();

            drawFrame();
        }

        std::ignore = m_device->waitIdle();
        return 0;
    }

    auto drawFrame() -> void
    {
        auto frameInfo = m_rhi.beginFrame();
        if (!frameInfo)
            return;

        auto& [cmd, swapchainImage, frameIndex] = *frameInfo;

        cmd.begin();
        cmd.beginLabel("Frame");
        cmd.transitionImageLayout({
            .imageRef = swapchainImage,
            .oldState = aegis::rhi::ResourceState::Unknown,
            .newState = aegis::rhi::ResourceState::Attachment,
        });
        cmd.transitionImageLayout({
            .imageRef = m_depthImage.ref(),
            .oldState = aegis::rhi::ResourceState::Unknown,
            .newState = aegis::rhi::ResourceState::Attachment,
        });

        cmd.beginLabel("Rendering");

        std::array colorAttachments{
            aegis::rhi::Attachment::color(
                swapchainImage,
                aegis::rhi::ClearColor{ 1.0, 1.0, 1.0, 1.0 }
            )
        };
        cmd.beginRendering(aegis::rhi::RenderingCmd{
            .colorAttachments = colorAttachments,
            .depthAttachment = aegis::rhi::Attachment::depth(
                m_depthImage.ref(),
                aegis::rhi::ClearDepthStencil{ 1.0f, 0 }
            ),
        });
        cmd.bindPipeline(m_pipeline);
        cmd.setViewport(swapchainImage.extent.toExtent2D());
        cmd.setScissor(swapchainImage.extent.toExtent2D());
        cmd.draw(3);
        cmd.endRendering();

        cmd.endLabel();

        cmd.transitionImageLayout({
            .imageRef = swapchainImage,
            .oldState = aegis::rhi::ResourceState::Attachment,
            .newState = aegis::rhi::ResourceState::Present,
        });

        cmd.endLabel();
        cmd.end();

        m_rhi.endFrame();
    }

    auto resize() -> void
    {
        std::ignore = m_device->waitIdle();

        aegis::rhi::Swapchain::Desc swapchainDesc{
            .context = m_context,
            .extent = aegis::rhi::Extent2D{ m_window.extent() },
            .oldSwapchain = &m_swapchain,
        };
        auto swapchain = m_device.createSwapchain(swapchainDesc);
        if (!swapchain)
        {
            std::println("Failed to recreate swapchain");
            return;
        }
        m_swapchain = std::move(*swapchain);

        aegis::rhi::Image::Desc depthImageDesc{
            .name = "depthImage",
            .extent = aegis::rhi::Extent3D{ swapchainDesc.extent },
            .format = aegis::rhi::Format::D32_SFLOAT,
            .usage = aegis::rhi::ImageUsage::DepthStencilAttachment,
        };
        auto depthImage = m_device.createImage(depthImageDesc);
        if (!depthImage)
        {
            std::println("Failed to recreate depth image");
            return;
        }
        m_depthImage = std::move(*depthImage);

        m_window.resetResized();
    }

    void upload()
    {
        m_uploadCmd.begin(true);
        m_uploadCmd.beginLabel("UploadCmdBuffer");

        m_uploadCmd.endLabel();
        m_uploadCmd.end();

        auto value = m_device.graphicsQueue().submit(m_uploadCmd);
        if (!value)
            return;
        m_device.graphicsQueue().wait(*value);
    }

private:
    aegis::platform::Window m_window;
    aegis::rhi::RHI m_rhi;

    aegis::rhi::Pipeline m_pipeline;
    aegis::rhi::Image m_depthImage;

    aegis::rhi::CommandPool m_uploadPool;
    aegis::rhi::CommandBuffer m_uploadCmd;
};

auto main() -> int
{
    auto engine = Engine::create();
    if (!engine)
    {
        std::println("Failed to create engine \n{}", engine.error());
        return 1;
    }
    return engine->run();
}
