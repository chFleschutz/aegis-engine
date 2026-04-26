module;
#include <functional>
#include <string>

export module aegis.rhi:device;
import :buffer;
import :texture;
import :pipeline;
import :command_buffer;

import vulkan_hpp;

export namespace aegis::rhi
{
class Device
{
public:
    struct Desc
    {
        std::string appName;
        std::function<vk::SurfaceKHR(vk::Instance)> createSurface;
    };

    explicit Device(const Desc& desc);
    ~Device() = default;

private:
    void createInstance(const Desc& desc);

    vk::raii::Context m_context;
    vk::raii::Instance m_instance{ nullptr };
    vk::raii::SurfaceKHR m_surface{ nullptr };
};
}
