module;
#include <expected>
#include <functional>
#include <string>

export module aegis.rhi:device;
import :context;
import :error;

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

        [[nodiscard]] auto isComplete() const
            -> bool;
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

    static constexpr auto requiredExtensions = std::array{
        vk::KHRSwapchainExtensionName,
        vk::EXTShaderObjectExtensionName
    };

    static auto create(const Desc& desc)
        -> std::expected<Device, Error>;

    [[nodiscard]] auto operator->() const
        -> const vk::raii::Device* { return &m_device; }

    [[nodiscard]] auto physicalDevice() const
        -> const vk::raii::PhysicalDevice&;

    [[nodiscard]] auto device() const
        -> const vk::raii::Device& { return m_device; }

    [[nodiscard]] auto queueFamilies() const
        -> const QueueFamilyIndices& { return m_queueFamilyIndices; }

    [[nodiscard]] auto properties() const
        -> const Properties& { return m_properties; }

    [[nodiscard]] auto capabilities() const
        -> const Capabilities& { return m_capabilities; }

private:
    using FeatureChain = vk::StructureChain<
        vk::DeviceCreateInfo,
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceShaderObjectFeaturesEXT,
        vk::PhysicalDeviceMeshShaderFeaturesEXT>;

    Device(
        vk::raii::PhysicalDevice pd,
        vk::raii::Device device,
        QueueFamilyIndices queueFamilyIndices,
        Capabilities capabilities);

    [[nodiscard]] static auto createPhysicalDevice(const Desc& desc)
        -> std::expected<vk::raii::PhysicalDevice, Error>;

    [[nodiscard]] static auto queryQueueFamilies(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface)
        -> QueueFamilyIndices;

    [[nodiscard]] static auto queryCapabilities(const vk::raii::PhysicalDevice& pd)
        -> Capabilities;

    [[nodiscard]] static auto createDevice(
        const vk::raii::PhysicalDevice& pd,
        const Capabilities& capabilities,
        const QueueFamilyIndices& queueFamilyIndices)
        -> std::expected<vk::raii::Device, Error>;

    [[nodiscard]] static auto queryExtensions(const Capabilities& caps)
        -> std::vector<const char*>;

    [[nodiscard]] static auto supportsExtensions(const vk::raii::PhysicalDevice& pd)
        -> bool;

    [[nodiscard]] static auto createFeatureChain()
        -> FeatureChain;

    [[nodiscard]] static auto queryProperties(const vk::raii::PhysicalDevice& pd)
        -> Properties;

    vk::raii::PhysicalDevice m_physicalDevice;
    vk::raii::Device m_device;
    vk::raii::Queue m_graphicsQueue;
    vk::raii::Queue m_computeQueue;
    vk::raii::Queue m_transferQueue;
    vk::raii::Queue m_presentQueue;
    QueueFamilyIndices m_queueFamilyIndices;
    Properties m_properties;
    Capabilities m_capabilities;
};
}
