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

struct FrameSync
{
    aegis::rhi::Semaphore imageAvailable;
    std::uint64_t timelineValue{ 0 };
};

auto createFrameSync(const aegis::rhi::Device& device)
    -> std::expected<std::array<FrameSync, 2>, std::string>
{
    auto s0 = aegis::rhi::Semaphore::create({ device });
    auto s1 = aegis::rhi::Semaphore::create({ device });

    if (!s0 || !s1)
        return std::unexpected{ "Failed to create frame sync objects" };

    return std::array{
        FrameSync{ std::move(*s0) },
        FrameSync{ std::move(*s1) },
    };
}

auto main()
    -> int
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
    {
        std::println("Failed to create rhi context");
        return 1;
    }

    aegis::rhi::Device::Desc deviceDesc{
        .context = *context,
    };
    auto device = aegis::rhi::Device::create(deviceDesc);
    if (!device)
    {
        std::println("Failed to create rhi device");
        return 1;
    }

    aegis::rhi::Swapchain::Desc swapchainDesc{
        .context = *context,
        .device = *device,
    };
    auto swapchain = aegis::rhi::Swapchain::create(swapchainDesc);
    if (!swapchain)
    {
        std::println("Failed to create swapchain");
        return 1;
    }

    auto shader = loadSPIRV(SHADER_PATH);
    if (!shader)
    {
        std::println("Failed to load shader from {}", SHADER_PATH);
        return 1;
    }

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
    {
        std::println("Failed to create pipeline");
        return 1;
    }

    aegis::rhi::CommandPool::Desc poolDesc{
        .device = *device,
        .queueFamily = device->graphicsQueue().family(),
    };
    auto commandPool = aegis::rhi::CommandPool::create(poolDesc);
    if (!commandPool)
    {
        std::println("Failed to create command pool");
        return 1;
    }

    aegis::rhi::CommandBuffer::Desc cmdBufferDesc{
        .device = *device,
        .pool = *commandPool,
    };
    auto cmd = aegis::rhi::CommandBuffer::create(cmdBufferDesc);
    if (!cmd)
    {
        std::println("Failed to create command buffer");
        return 1;
    }

    auto frameSync = createFrameSync(*device);
    if (!frameSync)
    {
        std::println("Failed to create frameSync");
        return 1;
    }

    std::uint32_t currentFrame = 0;
    while (!window.shouldClose())
    {
        window.pollEvents();

        auto& [imageAvailable, timePoint] = (*frameSync)[currentFrame];
        if (!device->graphicsQueue().wait(timePoint))
        {
            std::println("Failed to wait for frame sync fence");
            return 1;
        }

        auto acquiredImage = swapchain->acquireNextImage(imageAvailable);
        if (!acquiredImage)
        {
            std::println("Failed to acquire next swapchain image");
            return 1;
        }

        cmd->begin();
        // ...
        cmd->end();

        aegis::rhi::Queue::SubmitInfo submitInfo{
            .commandBuffer = *cmd,
            .waitSemaphore = imageAvailable,
            .signalSemaphore = acquiredImage->presentReady,
        };
        auto newSubmitTime = device->graphicsQueue().submit(submitInfo);
        if (!newSubmitTime)
        {
            std::println("Failed to submit to queue");
            return 1;
        }
        timePoint = *newSubmitTime;

        aegis::rhi::Queue::PresentInfo presentInfo{
            .swapchain = *swapchain,
            .waitSemaphore = acquiredImage->presentReady,
            .imageIndex = acquiredImage->imageIndex,
        };
        if (auto result = device->presentQueue().present(presentInfo); !result)
        {
            std::println("Failed to present to queue");
            return 1;
        }

        currentFrame = (currentFrame + 1) % 2;

    }
}
