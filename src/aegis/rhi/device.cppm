module;
#include <expected>
#include <functional>
#include <string>

export module aegis.rhi:device;
import :buffer;
import :command_buffer;
import :command_pool;
import :error;
import :image;
import :image_view;
import :memory;
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

    static constexpr std::array requiredExtensions{
        vk::KHRSwapchainExtensionName,
    };

    [[nodiscard]] auto operator->() const noexcept -> const vk::raii::Device* { return &m_device; }
    [[nodiscard]] auto operator*() const noexcept -> const vk::raii::Device& { return m_device; }

    [[nodiscard]] auto physicalDevice() const noexcept -> const vk::raii::PhysicalDevice&;
    [[nodiscard]] auto device() const noexcept -> const vk::raii::Device& { return m_device; }
    [[nodiscard]] auto allocator() const noexcept -> const Allocator& { return m_allocator; }
    [[nodiscard]] auto graphicsQueue() noexcept -> Queue& { return m_graphicsQueue; }
    [[nodiscard]] auto computeQueue() noexcept -> Queue& { return m_computeQueue; }
    [[nodiscard]] auto transferQueue() noexcept -> Queue& { return m_transferQueue; };
    [[nodiscard]] auto presentQueue() noexcept -> Queue& { return m_presentQueue; };
    [[nodiscard]] auto properties() const noexcept -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const noexcept -> const Capabilities& { return m_capabilities; }

    [[nodiscard]] auto createBuffer(const Buffer::Desc& desc) const -> std::expected<Buffer, Error>;

    [[nodiscard]] auto createCommandBuffer(const CommandBuffer::Desc& desc) const
        -> std::expected<CommandBuffer, Error>;

    [[nodiscard]] auto createCommandPool(const CommandPool::Desc& desc) const
        -> std::expected<CommandPool, Error>;

    [[nodiscard]] auto createFence(const Fence::Desc& desc) const
        -> std::expected<Fence, Error>;

    [[nodiscard]] auto createImage(const Image::Desc& desc) const
        -> std::expected<Image, Error>;

    [[nodiscard]] auto createImageView(const Image& image, const ImageView::Range& range,
        std::string_view name = {}) const
        -> std::expected<ImageView, Error>;

    [[nodiscard]] auto createPipeline(const Pipeline::GraphicsDesc& desc) const
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] auto createPipeline(const Pipeline::ComputeDesc& desc) const
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] auto createSemaphore(const Semaphore::Desc& desc) const
        -> std::expected<Semaphore, Error>;

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

    [[nodiscard]] static auto create(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface,
        const Desc& desc)
        -> std::expected<Device, Error>;

    [[nodiscard]] static auto createPhysicalDevice(const vk::raii::Instance& instance,
        const vk::raii::SurfaceKHR& surface)
        -> std::expected<vk::raii::PhysicalDevice, Error>;

    [[nodiscard]] static auto queryQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface)
        -> QueueFamilyIndices;

    [[nodiscard]] static auto createQueue(const vk::raii::Device& device, std::string_view name,
        std::uint32_t queueFamily)
        -> std::expected<Queue, Error>;

    [[nodiscard]] static auto createDevice(const vk::raii::PhysicalDevice& pd,
        const Capabilities& capabilities, const QueueFamilyIndices& queueFamilyIndices)
        -> std::expected<vk::raii::Device, Error>;

    [[nodiscard]] static auto queryCapabilities(const vk::raii::PhysicalDevice& pd) -> Capabilities;
    [[nodiscard]] static auto queryExtensions(const Capabilities& caps) -> std::vector<const char*>;
    [[nodiscard]] static auto supportsExtensions(const vk::raii::PhysicalDevice& pd) -> bool;
    [[nodiscard]] static auto createFeatureChain() -> FeatureChain;
    [[nodiscard]] static auto queryProperties(const vk::raii::PhysicalDevice& pd) -> Properties;

    Device(
        vk::raii::PhysicalDevice pd,
        vk::raii::Device device,
        Allocator allocator,
        Queue graphicsQueue,
        Queue computeQueue,
        Queue transferQueue,
        Queue presentQueue,
        Capabilities capabilities);

    vk::raii::PhysicalDevice m_physicalDevice;
    vk::raii::Device m_device;
    Allocator m_allocator;
    Queue m_graphicsQueue;
    Queue m_computeQueue;
    Queue m_transferQueue;
    Queue m_presentQueue;
    Properties m_properties;
    Capabilities m_capabilities;
};
}
