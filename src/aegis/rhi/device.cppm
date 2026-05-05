module;
#include <GLFW/glfw3.h>

#include <functional>
#include <string>
#include <vector>

export module aegis.rhi:device;
import :buffer;
import :texture;
import :pipeline;
import :command_buffer;

import aegis.platform.window;

import vulkan_hpp;

export namespace aegis::rhi
{
class Device
{
public:
    struct Desc
    {
        std::string appName;
        platform::Window& window;
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

#ifdef NDEBUG
    static constexpr bool enableValidation = false;
#else
    static constexpr bool enableValidation = true;
#endif

    explicit Device(const Desc& desc);
    ~Device() = default;

    [[nodiscard]] auto properties() const -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const -> const Capabilities& { return m_capabilities; }

private:
    struct QueueFamilyIndices
    {
        uint32_t graphics{ vk::QueueFamilyIgnored };
        uint32_t present{ vk::QueueFamilyIgnored };

        [[nodiscard]] auto isComplete() const -> bool
        {
            return graphics != vk::QueueFamilyIgnored && present != vk::QueueFamilyIgnored;
        }
    };

    void createInstance(const Desc& desc);
    void createDebugMessenger(const Desc& desc);
    void createSurface(const Desc& desc);
    void createPhysicalDevice(const Desc& desc);
    void createDevice(const Desc& desc);

    [[nodiscard]] auto findQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice) const
        -> QueueFamilyIndices;
    [[nodiscard]] static auto findExtensions() -> std::vector<const char*>;
    [[nodiscard]] auto findLayers() const -> std::vector<const char*>;

    vk::raii::Context m_context;
    vk::raii::Instance m_instance{ nullptr };
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger{ nullptr };
    vk::raii::SurfaceKHR m_surface{ nullptr };
    vk::raii::PhysicalDevice m_physicalDevice{ nullptr };
    vk::raii::Device m_device{ nullptr };
    vk::raii::Queue m_queue{ nullptr };
    Properties m_properties;
    Capabilities m_capabilities;
};
}
