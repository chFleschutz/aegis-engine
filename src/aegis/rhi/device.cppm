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

    struct Properties
    {
        vk::PhysicalDeviceProperties2 core;
        vk::PhysicalDeviceVulkan11Properties vk11;
        vk::PhysicalDeviceVulkan12Properties vk12;
        vk::PhysicalDeviceVulkan13Properties vk13;
    };

    struct Capabilities
    {
        bool meshShaders = false;
    };

    explicit Device(const Desc& desc);
    ~Device() = default;

    [[nodiscard]] auto properties() const -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const -> const Capabilities& { return m_capabilities; }

private:
    void createInstance(const Desc& desc);
    void createPhysicalDevice(const Desc& desc);

    vk::raii::Context m_context;
    vk::raii::Instance m_instance{ nullptr };
    vk::raii::SurfaceKHR m_surface{ nullptr };
    vk::raii::PhysicalDevice m_physicalDevice{ nullptr };
    Properties m_properties;
    Capabilities m_capabilities;
};
}
