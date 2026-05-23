module;
#include <algorithm>
#include <expected>

module aegis.rhi;
import :device;
import :context;
import :error;
import :vulkan;

namespace aegis::rhi
{
auto Device::QueueFamilyIndices::isComplete() const -> bool
{
    return graphics != vk::QueueFamilyIgnored && present != vk::QueueFamilyIgnored &&
           compute != vk::QueueFamilyIgnored && transfer != vk::QueueFamilyIgnored;
}

Device::Device(
    vk::raii::PhysicalDevice pd,
    vk::raii::Device device,
    Queue graphicsQueue,
    Queue computeQueue,
    Queue transferQueue,
    Queue presentQueue,
    DeviceCapabilities capabilities) :
    m_physicalDevice{ std::move(pd) },
    m_device{ std::move(device) },
    m_graphicsQueue{ std::move(graphicsQueue) },
    m_computeQueue{ std::move(computeQueue) },
    m_transferQueue{ std::move(transferQueue) },
    m_presentQueue{ std::move(presentQueue) },
    m_capabilities{ capabilities }
{
}

auto Device::physicalDevice() const -> const vk::raii::PhysicalDevice&
{
    return m_physicalDevice;
}

auto Device::queryProperties(const vk::raii::PhysicalDevice& pd) -> Properties
{
    auto props = pd.getProperties2<
        vk::PhysicalDeviceProperties2,
        vk::PhysicalDeviceVulkan11Properties,
        vk::PhysicalDeviceVulkan12Properties,
        vk::PhysicalDeviceVulkan13Properties>();

    return Properties{
        .core = props.get<vk::PhysicalDeviceProperties2>(),
        .vk11 = props.get<vk::PhysicalDeviceVulkan11Properties>(),
        .vk12 = props.get<vk::PhysicalDeviceVulkan12Properties>(),
        .vk13 = props.get<vk::PhysicalDeviceVulkan13Properties>(),
    };
}
}
