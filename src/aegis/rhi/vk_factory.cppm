module;
#include <expected>
#include <span>
#include <vector>

export module aegis.rhi:vk_factory;
import :error;
import :queue;
import vulkan_hpp;

namespace aegis::rhi::vk_factory
{
using FeatureChain = vk::StructureChain<
    vk::DeviceCreateInfo,
    vk::PhysicalDeviceFeatures2,
    vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan12Features,
    vk::PhysicalDeviceVulkan13Features,
    vk::PhysicalDeviceShaderObjectFeaturesEXT,
    vk::PhysicalDeviceMeshShaderFeaturesEXT>;

[[nodiscard]] auto createPhysicalDevice(
    const vk::raii::Instance& instance,
    const vk::raii::SurfaceKHR& surface,
    std::span<const char* const> requiredExtensions)
    -> std::expected<vk::raii::PhysicalDevice, Error>;

[[nodiscard]] auto queryQueueFamilies(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface)
    -> QueueFamilyIndices;

[[nodiscard]] auto supportsExtensions(
    const vk::raii::PhysicalDevice& pd,
    std::span<const char* const> requiredExtensions)
    -> bool;

[[nodiscard]] auto createDevice(
    const vk::raii::PhysicalDevice& pd,
    const DeviceCapabilities& capabilities,
    const QueueFamilyIndices& queueFamilyIndices,
    std::span<const char* const> requiredExtensions)
    -> std::expected<vk::raii::Device, Error>;

[[nodiscard]] auto createFeatureChain() -> FeatureChain;

[[nodiscard]] auto queryCapabilities(const vk::raii::PhysicalDevice& pd)
    -> DeviceCapabilities;

[[nodiscard]] auto queryExtensions(
    const DeviceCapabilities& caps,
    std::span<const char* const> requiredExtensions)
    -> std::vector<const char*>;

[[nodiscard]] auto createQueue(
    const vk::raii::Device& device,
    std::uint32_t queueFamily)
    -> std::expected<Queue, Error>;
}
