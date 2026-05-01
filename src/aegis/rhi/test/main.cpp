
import aegis.rhi;
import vulkan_hpp;

auto main() -> int
{
    aegis::rhi::Device::Desc desc{ //
        .createSurface = [](vk::Instance) -> vk::SurfaceKHR { return nullptr; }
    };
    aegis::rhi::Device device{ desc };
}
