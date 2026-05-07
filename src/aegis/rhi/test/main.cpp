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
    aegis::rhi::Device device{ deviceDesc };

    aegis::rhi::Swapchain::Desc swapchainDesc{
        .context = *context,
        .device = device,
    };
    aegis::rhi::Swapchain swapchain{ swapchainDesc };
}
