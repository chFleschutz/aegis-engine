module;
#include <expected>
#include <functional>
#include <span>
#include <string>

export module aegis.rhi:device;
import :error;
import :fwd;
import :queue;
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

    struct QueueFamilyIndices
    {
        uint32_t graphics{ vk::QueueFamilyIgnored };
        uint32_t compute{ vk::QueueFamilyIgnored };
        uint32_t transfer{ vk::QueueFamilyIgnored };
        uint32_t present{ vk::QueueFamilyIgnored };

        [[nodiscard]] auto isComplete() const -> bool;
    };

    struct Properties
    {
        vk::PhysicalDeviceProperties2 core;
        vk::PhysicalDeviceVulkan11Properties vk11;
        vk::PhysicalDeviceVulkan12Properties vk12;
        vk::PhysicalDeviceVulkan13Properties vk13;
    };

#ifdef NDEBUG
    static constexpr bool enableValidation = false;
#else
    static constexpr bool enableValidation = true;
#endif

    static constexpr std::array requiredExtensions{
        vk::KHRSwapchainExtensionName,
        vk::EXTShaderObjectExtensionName
    };

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<Device, Error>;

    Device(
        vk::raii::PhysicalDevice pd,
        vk::raii::Device device,
        Queue graphicsQueue,
        Queue computeQueue,
        Queue transferQueue,
        Queue presentQueue,
        DeviceCapabilities capabilities);

    [[nodiscard]] auto operator->() const -> const vk::raii::Device* { return &m_device; }

    [[nodiscard]] auto physicalDevice() const -> const vk::raii::PhysicalDevice&;
    [[nodiscard]] auto device() const -> const vk::raii::Device& { return m_device; }
    [[nodiscard]] auto graphicsQueue() -> Queue& { return m_graphicsQueue; }
    [[nodiscard]] auto computeQueue() -> Queue& { return m_computeQueue; }
    [[nodiscard]] auto transferQueue() -> Queue& { return m_transferQueue; };
    [[nodiscard]] auto presentQueue() -> Queue& { return m_presentQueue; };
    [[nodiscard]] auto properties() const -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const -> const DeviceCapabilities& { return m_capabilities; }

private:

    [[nodiscard]] static auto queryProperties(const vk::raii::PhysicalDevice& pd) -> Properties;

    vk::raii::PhysicalDevice m_physicalDevice;
    vk::raii::Device m_device;
    Queue m_graphicsQueue;
    Queue m_computeQueue;
    Queue m_transferQueue;
    Queue m_presentQueue;
    Properties m_properties;
    DeviceCapabilities m_capabilities;
};
}
