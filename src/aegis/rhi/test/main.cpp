import aegis.rhi;
import aegis.platform.window;
import vulkan_hpp;

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

auto main() -> int
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
}
