import aegis.rhi;
import aegis.platform.window;
import vulkan_hpp;

auto main() -> int
{
    aegis::platform::Window::Desc windowDesc{
        .title = "Test Window",
        .width = 640,
        .height = 480,
    };
    aegis::platform::Window window{ windowDesc };

    aegis::rhi::Device::Desc deviceDesc{
        .appName = "Test",
        .window = window,
    };
    aegis::rhi::Device device{ deviceDesc };
}
