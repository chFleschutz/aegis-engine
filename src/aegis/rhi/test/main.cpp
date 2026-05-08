import aegis.rhi;
import aegis.platform.window;
import vulkan_hpp;

#include <print>

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
}
