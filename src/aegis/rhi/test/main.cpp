import aegis.rhi;
import aegis.platform.window;

#include <array>
#include <expected>
#include <filesystem>
#include <fstream>
#include <print>

using SpirvBuffer = std::vector<uint32_t>;

auto loadSPIRV(const std::filesystem::path& path)
    -> std::expected<SpirvBuffer, std::string>
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

struct FrameContext
{
    aegis::rhi::CommandBuffer commandBuffer;
    aegis::rhi::Semaphore imageAvailable;
    std::uint64_t timelineValue{ 0 };
};

constexpr uint32_t framesInFlight = 2;

auto createFrameContext(const aegis::rhi::Device& device,
    const aegis::rhi::CommandPool& pool)
    -> std::expected<std::vector<FrameContext>, std::string>
{
    std::vector<FrameContext> frameContext;
    frameContext.reserve(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        auto cmd = aegis::rhi::CommandBuffer::create({ device, pool });
        if (!cmd)
            return std::unexpected{ "Failed to create frame command buffer" };

        auto semaphore = aegis::rhi::Semaphore::create({ device });
        if (!semaphore)
            return std::unexpected{ "Failed to create frame semaphore" };

        frameContext.emplace_back(FrameContext{ std::move(*cmd), std::move(*semaphore) });
    }
    return frameContext;
}

class Application
{
public:
    static auto create()
        -> std::expected<Application, std::string>
    {
        aegis::platform::Window::Desc windowDesc{
            .title = "Test Window",
            .width = 640,
            .height = 480,
        };
        aegis::platform::Window window{ windowDesc };

        aegis::rhi::Context::Desc contextDesc{
            .appName = "Test",
            .window = window,
        };
        auto context = aegis::rhi::Context::create(contextDesc);
        if (!context)
            return std::unexpected{
                std::format("Failed to create rhi context")
            };

        aegis::rhi::Device::Desc deviceDesc{
            .context = *context,
        };
        auto device = aegis::rhi::Device::create(deviceDesc);
        if (!device)
            return std::unexpected{
                std::format("Failed to create rhi device")
            };

        aegis::rhi::Swapchain::Desc swapchainDesc{
            .context = *context,
            .device = *device,
        };
        auto swapchain = aegis::rhi::Swapchain::create(swapchainDesc);
        if (!swapchain)
            return std::unexpected{
                std::format("Failed to create swapchain")
            };

        aegis::rhi::CommandPool::Desc poolDesc{
            .device = *device,
            .queueFamily = device->graphicsQueue().family(),
        };
        auto commandPool = aegis::rhi::CommandPool::create(poolDesc);
        if (!commandPool)
            return std::unexpected{
                std::format("Failed to create command pool")
            };

        auto shader = loadSPIRV(SHADER_PATH);
        if (!shader)
            return std::unexpected{
                std::format("Failed to load shader from {}", SHADER_PATH)
            };

        auto colorAttachments = std::array{ swapchain->surfaceFormat() };
        auto shaders = std::array{
            aegis::rhi::Pipeline::Shader{
                .stage = aegis::rhi::ShaderStage::Vertex,
                .code = *shader,
                .entryPoint = "vertexMain",
            },
            aegis::rhi::Pipeline::Shader{
                .stage = aegis::rhi::ShaderStage::Fragment,
                .code = *shader,
                .entryPoint = "fragmentMain",
            },
        };
        aegis::rhi::Pipeline::GraphicsDesc pipelineDesc{
            .device = *device,
            .setLayouts = {},
            .pushConstantRanges = {},
            .shaders = shaders,
            .colorAttachments = colorAttachments,
            .depthAttachment = aegis::rhi::Format::D32_SFLOAT,
        };
        auto pipeline = aegis::rhi::Pipeline::create(pipelineDesc);
        if (!pipeline)
            return std::unexpected{
                std::format("Failed to create pipeline")
            };

        auto frameContext = createFrameContext(*device, *commandPool);
        if (!frameContext)
            return std::unexpected{
                std::format("Failed to create frameSync: {}", frameContext.error())
            };

        return std::expected<Application, std::string>{
            std::in_place,
            std::move(window),
            std::move(*context),
            std::move(*device),
            std::move(*swapchain),
            std::move(*commandPool),
            std::move(*pipeline),
            std::move(*frameContext)
        };
    }

    Application(aegis::platform::Window window,
        aegis::rhi::Context context,
        aegis::rhi::Device device,
        aegis::rhi::Swapchain swapchain,
        aegis::rhi::CommandPool pool,
        aegis::rhi::Pipeline pipeline,
        std::vector<FrameContext> frameContext) :
        m_window{ std::move(window) },
        m_context{ std::move(context) },
        m_device{ std::move(device) },
        m_swapchain{ std::move(swapchain) },
        m_commandPool{ std::move(pool) },
        m_pipeline{ std::move(pipeline) },
        m_frameContext{ std::move(frameContext) }
    {
    }

    auto run() -> int
    {
        std::uint32_t currentFrame = 0;
        while (!m_window.shouldClose())
        {
            m_window.pollEvents();

            auto& [cmd, imageAvailable, timePoint] = m_frameContext[currentFrame];
            if (!m_device.graphicsQueue().wait(timePoint))
            {
                std::println("Failed to wait for frame sync fence");
                return 1;
            }

            auto acquiredImage = m_swapchain.acquireNextImage(imageAvailable);
            if (!acquiredImage)
            {
                std::println("Failed to acquire next swapchain image");
                return 1;
            }

            cmd.begin();

            cmd.transitionImageLayout({
                .image = acquiredImage->image,
                .oldState = aegis::rhi::ResourceState::Unknown,
                .newState = aegis::rhi::ResourceState::RenderTarget,
            });
            auto attachmentDesc = std::array{
                aegis::rhi::CommandBuffer::AttachmentDesc{
                    .imageView = acquiredImage->view,
                }
            };
            cmd.beginRendering({ m_swapchain.extent(), attachmentDesc });
            cmd.bindPipeline(m_pipeline);
            cmd.setViewport(m_swapchain.extent().x, m_swapchain.extent().y);
            cmd.setScissor(m_swapchain.extent().x, m_swapchain.extent().y);
            cmd.draw(3);
            cmd.endRendering();

            cmd.transitionImageLayout({
                .image = acquiredImage->image,
                .oldState = aegis::rhi::ResourceState::RenderTarget,
                .newState = aegis::rhi::ResourceState::Present,
            });
            cmd.end();

            aegis::rhi::Queue::SubmitInfo submitInfo{
                .commandBuffer = cmd,
                .waitSemaphore = imageAvailable,
                .signalSemaphore = acquiredImage->presentReady,
            };
            auto newSubmitTime = m_device.graphicsQueue().submit(submitInfo);
            if (!newSubmitTime)
            {
                std::println("Failed to submit to queue");
                return 1;
            }
            timePoint = *newSubmitTime;

            aegis::rhi::Queue::PresentInfo presentInfo{
                .swapchain = m_swapchain,
                .waitSemaphore = acquiredImage->presentReady,
                .imageIndex = acquiredImage->imageIndex,
            };
            if (auto result = m_device.presentQueue().present(presentInfo); !result)
            {
                std::println("Failed to present to queue");
                return 1;
            }

            currentFrame = (currentFrame + 1) % framesInFlight;
        }

        std::ignore = m_device->waitIdle();
        return 0;
    }

private:
    aegis::platform::Window m_window;
    aegis::rhi::Context m_context;
    aegis::rhi::Device m_device;
    aegis::rhi::Swapchain m_swapchain;
    aegis::rhi::CommandPool m_commandPool;
    aegis::rhi::Pipeline m_pipeline;
    std::vector<FrameContext> m_frameContext;
};

auto main()
    -> int
{
    auto app = Application::create();
    if (!app)
    {
        std::println("Failed to create application \n{}", app.error());
        return 1;
    }
    return app->run();
}
