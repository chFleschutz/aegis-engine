module;
#include <functional>
#include <string>

export module aegis.rhi:device;
import :buffer;
import :command_buffer;
import :context;
import :texture;
import :pipeline;

import aegis.platform.window;

import vulkan_hpp;

export namespace aegis::rhi
{
class Device
{
public:
    struct Desc
    {
        const Context& context;
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

    [[nodiscard]] auto physicalDevice() const -> const vk::raii::PhysicalDevice&;
    [[nodiscard]] auto device() const -> const vk::raii::Device& { return m_device; }
    [[nodiscard]] auto properties() const -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const -> const Capabilities& { return m_capabilities; }

private:
    struct QueueFamilyIndices
    {
        uint32_t graphics{ vk::QueueFamilyIgnored };
        uint32_t compute{ vk::QueueFamilyIgnored };
        uint32_t transfer{ vk::QueueFamilyIgnored };
        uint32_t present{ vk::QueueFamilyIgnored };

        [[nodiscard]] auto isComplete() const -> bool
        {
            return graphics != vk::QueueFamilyIgnored && present != vk::QueueFamilyIgnored &&
                compute != vk::QueueFamilyIgnored && transfer != vk::QueueFamilyIgnored;
        }
    };

    void createPhysicalDevice(const Desc& desc);
    void createDevice(const Desc& desc);
    void createQueues(const Desc& desc);

    [[nodiscard]] static auto findQueueFamilies(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface) -> QueueFamilyIndices;

    vk::raii::PhysicalDevice m_physicalDevice{ nullptr };
    vk::raii::Device m_device{ nullptr };
    vk::raii::Queue m_graphicsQueue{ nullptr };
    vk::raii::Queue m_computeQueue{ nullptr };
    vk::raii::Queue m_transferQueue{ nullptr };
    vk::raii::Queue m_presentQueue{ nullptr };

    QueueFamilyIndices m_queueFamilyIndices{};
    Properties m_properties;
    Capabilities m_capabilities;
};
}
