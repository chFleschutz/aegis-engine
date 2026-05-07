module;
#include <algorithm>
#include <cassert>
#include <format>
#include <print>
#include <set>
#include <vector>

module aegis.rhi;
import :device;

namespace aegis::rhi
{
Device::Device(const Desc& desc)
{
    createPhysicalDevice(desc);
    createDevice(desc);
    createQueues(desc);
}

auto Device::physicalDevice() const -> const vk::raii::PhysicalDevice&
{
    return m_physicalDevice;
}

void Device::createPhysicalDevice(const Desc& desc)
{
    auto [result, physicalDevices] = desc.context.instance().enumeratePhysicalDevices();
    if (result != vk::Result::eSuccess || physicalDevices.empty())
    {
        // TODO: log error
        assert(false && "Vulkan Error: Failed to enumerate physical devices");
        return;
    }

    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (size_t i = 0; i < physicalDevices.size(); ++i)
    {
        const auto& physicalDevice = physicalDevices[i];
        uint32_t score{ 0 };

        if (!findQueueFamilies(physicalDevice, desc.context.surface()).isComplete())
            continue;

        auto features = physicalDevice.getFeatures2< //
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceMeshShaderFeaturesEXT>();
        const auto& vk10features = features.get<vk::PhysicalDeviceFeatures2>();
        const auto& vk11features = features.get<vk::PhysicalDeviceVulkan11Features>();
        const auto& vk12features = features.get<vk::PhysicalDeviceVulkan12Features>();
        const auto& vk13features = features.get<vk::PhysicalDeviceVulkan13Features>();
        const auto& meshShaderFeatures = features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
        // TODO: Check features

        if (!checkExtensionSupport(physicalDevice))
            continue;

        const auto properties = physicalDevice.getProperties();
        if (properties.apiVersion < vk::ApiVersion13)
            continue;

        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            score += 1000;

        score += properties.limits.maxImageDimension2D;
        candidates.emplace_back(score, i);
    }

    std::ranges::sort(candidates);
    if (candidates.empty() || candidates.front().first == 0)
    {
        assert(false && "Vulkan Error: Failed to find suitable physical device");
        return;
    }

    auto [_, index] = candidates.front();
    m_physicalDevice = physicalDevices[index];
    std::println("Physical device picked");
}

void Device::createDevice(const Desc& desc)
{
    vk::StructureChain<
        vk::DeviceCreateInfo,
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceMeshShaderFeaturesEXT>
        featureChain;

    featureChain.get<vk::PhysicalDeviceFeatures2>().features.setSamplerAnisotropy(true);

    featureChain
        .get<vk::PhysicalDeviceVulkan11Features>() //
        .setShaderDrawParameters(true);

    featureChain.get<vk::PhysicalDeviceVulkan12Features>()
        .setStorageBuffer8BitAccess(true)
        .setUniformAndStorageBuffer8BitAccess(true)
        .setStoragePushConstant8(true)
        .setShaderInt8(true)
        .setDescriptorIndexing(true)
        .setShaderUniformBufferArrayNonUniformIndexing(true)
        .setShaderSampledImageArrayNonUniformIndexing(true)
        .setShaderStorageBufferArrayNonUniformIndexing(true)
        .setShaderStorageImageArrayNonUniformIndexing(true)
        .setDescriptorBindingUniformBufferUpdateAfterBind(true)
        .setDescriptorBindingSampledImageUpdateAfterBind(true)
        .setDescriptorBindingStorageImageUpdateAfterBind(true)
        .setDescriptorBindingStorageBufferUpdateAfterBind(true)
        .setDescriptorBindingUpdateUnusedWhilePending(true)
        .setDescriptorBindingPartiallyBound(true)
        .setDescriptorBindingVariableDescriptorCount(true)
        .setRuntimeDescriptorArray(true)
        .setScalarBlockLayout(true)
        .setUniformBufferStandardLayout(true);

    featureChain.get<vk::PhysicalDeviceVulkan13Features>()
        .setShaderDemoteToHelperInvocation(true)
        .setDynamicRendering(true)
        .setMaintenance4(true);

    featureChain
        .get<vk::PhysicalDeviceMeshShaderFeaturesEXT>() //
        .setMeshShader(true)
        .setTaskShader(true);

    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice, desc.context.surface());
    std::set uniqueQueueFamilies{
        m_queueFamilyIndices.graphics,
        m_queueFamilyIndices.compute,
        m_queueFamilyIndices.transfer,
        m_queueFamilyIndices.present,
    };

    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t family : uniqueQueueFamilies)
    {
        queueCreateInfos.emplace_back(
            vk::DeviceQueueCreateInfo{
                .queueFamilyIndex = family,
                .queueCount = 1,
                .pQueuePriorities = &queuePriority,
            });
    }

    vk::DeviceCreateInfo deviceInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data(),
    };

    auto [result, device] = m_physicalDevice.createDevice(deviceInfo);
    if (result != vk::Result::eSuccess)
    {
        // TODO: error
        assert(false && "Vulkan Error: Failed to create device");
        return;
    }

    m_device = std::move(device);
    std::println("Device created");
}
void Device::createQueues(const Desc& desc)
{
    m_graphicsQueue = m_device.getQueue(m_queueFamilyIndices.graphics, 0);
    m_computeQueue = m_device.getQueue(m_queueFamilyIndices.compute, 0);
    m_transferQueue = m_device.getQueue(m_queueFamilyIndices.transfer, 0);
    m_presentQueue = m_device.getQueue(m_queueFamilyIndices.present, 0);
}

auto Device::findQueueFamilies(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface) -> QueueFamilyIndices
{
    QueueFamilyIndices indices;

    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& props = queueFamilies[i];
        bool hasGraphics = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eGraphics);
        bool hasCompute = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eCompute);
        bool hasTransfer = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eTransfer);

        if (hasGraphics)
            indices.graphics = i;

        // Dedicated Compute Queue
        if (hasCompute && !hasGraphics && !hasTransfer)
            indices.compute = i;

        // Dedicated Transfer Queue
        if (hasTransfer && !hasGraphics && !hasCompute)
            indices.transfer = i;

        auto [result, hasPresent] = physicalDevice.getSurfaceSupportKHR(i, *surface);
        if (result != vk::Result::eSuccess)
            continue;

        if (hasPresent)
            indices.present = i;

        if (indices.isComplete())
            break;
    }

    // Fallback if no dedicated queues are present
    if (indices.compute == vk::QueueFamilyIgnored)
        indices.compute = indices.graphics;
    if (indices.transfer == vk::QueueFamilyIgnored)
        indices.transfer = indices.graphics;

    return indices;
}

auto Device::checkExtensionSupport(const vk::raii::PhysicalDevice& pd) -> bool
{
    auto [result, availableExtensions] = pd.enumerateDeviceExtensionProperties();
    if (result != vk::Result::eSuccess)
    {
        // TODO: Error
        return false;
    }

    return std::ranges::all_of(requiredExtensions, [&availableExtensions](const auto& required) {
        return std::ranges::any_of(availableExtensions, [&required](const auto& available) {
            return std::string_view{ available.extensionName } == std::string_view{ required };
        });
    });
}
}
}
