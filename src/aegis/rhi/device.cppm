module;
#include <expected>
#include <functional>
#include <string>

export module aegis.rhi:device;
import :command_buffer;
import :command_pool;
import :error;
import :pipeline;
import :queue;
import :swapchain;
import aegis.platform.window;
import vulkan_hpp;

export namespace aegis::rhi
{
class Device
{
    friend class Context;

public:
    struct Desc
    {
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
        bool meshShaders{ false };
    };

#ifdef NDEBUG
    static constexpr bool enableValidation{ false };
#else
    static constexpr bool enableValidation{ true };
#endif

    static constexpr std::array requiredExtensions{
        vk::KHRSwapchainExtensionName,
        vk::EXTShaderObjectExtensionName
    };

    [[nodiscard]] auto operator->() const -> const vk::raii::Device* { return &m_device; }

    [[nodiscard]] auto physicalDevice() const -> const vk::raii::PhysicalDevice&;
    [[nodiscard]] auto device() const -> const vk::raii::Device& { return m_device; }
    [[nodiscard]] auto graphicsQueue() -> Queue& { return m_graphicsQueue; }
    [[nodiscard]] auto computeQueue() -> Queue& { return m_computeQueue; }
    [[nodiscard]] auto transferQueue() -> Queue& { return m_transferQueue; };
    [[nodiscard]] auto presentQueue() -> Queue& { return m_presentQueue; };
    [[nodiscard]] auto properties() const -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const -> const Capabilities& { return m_capabilities; }

    [[nodiscard]] auto createCommandBuffer(const CommandBuffer::Desc& desc) const
        -> std::expected<CommandBuffer, Error>;
    [[nodiscard]] auto createCommandPool(const CommandPool::Desc& desc) const
        -> std::expected<CommandPool, Error>;
    [[nodiscard]] auto createPipeline(const Pipeline::GraphicsDesc& desc) const
        -> std::expected<Pipeline, Error>;
    [[nodiscard]] auto createPipeline(const Pipeline::ComputeDesc& desc) const
        -> std::expected<Pipeline, Error>;
    [[nodiscard]] auto createSwapchain(const Swapchain::Desc& desc) const
        -> std::expected<Swapchain, Error>;

private:
    using FeatureChain = vk::StructureChain<
        vk::DeviceCreateInfo,
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceShaderObjectFeaturesEXT,
        vk::PhysicalDeviceMeshShaderFeaturesEXT>;

    struct QueueFamilyIndices
    {
        uint32_t graphics{ vk::QueueFamilyIgnored };
        uint32_t compute{ vk::QueueFamilyIgnored };
        uint32_t transfer{ vk::QueueFamilyIgnored };
        uint32_t present{ vk::QueueFamilyIgnored };

        [[nodiscard]] auto isComplete() const -> bool;
    };

    [[nodiscard]] static auto create(
        const vk::raii::Instance& instance,
        const vk::raii::SurfaceKHR& surface,
        const Desc& desc)
        -> std::expected<Device, Error>;

    [[nodiscard]] static auto createPhysicalDevice(
        const vk::raii::Instance& instance,
        const vk::raii::SurfaceKHR& surface)
        -> std::expected<vk::raii::PhysicalDevice, Error>;

    [[nodiscard]] static auto queryQueueFamilies(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface)
        -> QueueFamilyIndices;

    [[nodiscard]] static auto createQueue(
        const vk::raii::Device& device,
        std::uint32_t queueFamily)
        -> std::expected<Queue, Error>;

    [[nodiscard]] static auto createDevice(
        const vk::raii::PhysicalDevice& pd,
        const Capabilities& capabilities,
        const QueueFamilyIndices& queueFamilyIndices)
        -> std::expected<vk::raii::Device, Error>;

    [[nodiscard]] static auto queryCapabilities(const vk::raii::PhysicalDevice& pd) -> Capabilities;
    [[nodiscard]] static auto queryExtensions(const Capabilities& caps) -> std::vector<const char*>;
    [[nodiscard]] static auto supportsExtensions(const vk::raii::PhysicalDevice& pd) -> bool;
    [[nodiscard]] static auto createFeatureChain() -> FeatureChain;
    [[nodiscard]] static auto queryProperties(const vk::raii::PhysicalDevice& pd) -> Properties;

    Device(
        vk::raii::PhysicalDevice pd,
        vk::raii::Device device,
        Queue graphicsQueue,
        Queue computeQueue,
        Queue transferQueue,
        Queue presentQueue,
        Capabilities capabilities);

    vk::raii::PhysicalDevice m_physicalDevice;
    vk::raii::Device m_device;
    Queue m_graphicsQueue;
    Queue m_computeQueue;
    Queue m_transferQueue;
    Queue m_presentQueue;
    Properties m_properties;
    Capabilities m_capabilities;
};
}
