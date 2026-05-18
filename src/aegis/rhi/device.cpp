module;
#include <algorithm>
#include <expected>
#include <format>
#include <ranges>
#include <set>
#include <vector>

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

auto Device::create(const Desc& desc) -> std::expected<Device, Error>
{
    auto physicalDevice = createPhysicalDevice(desc);
    if (!physicalDevice)
        return std::unexpected{ physicalDevice.error() };

    auto queueFamilies = queryQueueFamilies(*physicalDevice, desc.context.surface());
    auto capabilities = queryCapabilities(*physicalDevice);

    auto device = createDevice(*physicalDevice, capabilities, queueFamilies);
    if (!device)
        return std::unexpected{ device.error() };

    auto graphicsQueue = createQueue(*device, queueFamilies.graphics);
    if (!graphicsQueue)
        return std::unexpected{ graphicsQueue.error() };

    auto computeQueue = createQueue(*device, queueFamilies.compute);
    if (!computeQueue)
        return std::unexpected{ computeQueue.error() };

    auto transferQueue = createQueue(*device, queueFamilies.transfer);
    if (!transferQueue)
        return std::unexpected{ transferQueue.error() };

    auto presentQueue = createQueue(*device, queueFamilies.present);
    if (!presentQueue)
        return std::unexpected{ presentQueue.error() };

    return std::expected<Device, Error>{
        std::in_place,
        std::move(*physicalDevice),
        std::move(*device),
        std::move(*graphicsQueue),
        std::move(*computeQueue),
        std::move(*transferQueue),
        std::move(*presentQueue),
        capabilities
    };
}

Device::Device(
    vk::raii::PhysicalDevice pd,
    vk::raii::Device device,
    Queue graphicsQueue,
    Queue computeQueue,
    Queue transferQueue,
    Queue presentQueue,
    Capabilities capabilities) :
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

auto Device::createPhysicalDevice(const Desc& desc)
    -> std::expected<vk::raii::PhysicalDevice, Error>
{
    auto [result, physicalDevices] = desc.context.instance().enumeratePhysicalDevices();
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    if (physicalDevices.empty())
        return makeError(ErrorCode::Unknown);

    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (size_t i = 0; i < physicalDevices.size(); ++i)
    {
        const auto& physicalDevice = physicalDevices[i];
        uint32_t score{ 0 };

        if (!queryQueueFamilies(physicalDevice, desc.context.surface()).isComplete())
            continue;

        // TODO: handle missing capabilities
        // auto capabilities = queryCapabilities(physicalDevice);

        if (!supportsExtensions(physicalDevice))
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
        return makeError(ErrorCode::Unknown);

    const auto& [_, index] = candidates.front();
    return physicalDevices[index];
}

auto Device::queryQueueFamilies(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface)
    -> QueueFamilyIndices
{
    QueueFamilyIndices indices;

    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& props = queueFamilies[i];
        auto hasGraphics = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eGraphics);
        auto hasCompute = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eCompute);
        auto hasTransfer = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eTransfer);

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

auto Device::createQueue(
    const vk::raii::Device& device,
    std::uint32_t queueFamily)
    -> std::expected<Queue, Error>
{
    auto queue = device.getQueue(queueFamily, 0);

    auto semaphoreTypeInfo = vk::SemaphoreTypeCreateInfo{
        .semaphoreType = vk::SemaphoreType::eTimeline,
    };
    auto semaphoreInfo = vk::SemaphoreCreateInfo{
        .pNext = &semaphoreTypeInfo,
    };
    auto semaphore = device.createSemaphore(semaphoreInfo);
    if (!semaphore.has_value())
        return makeError(toRHI(semaphore.result));

    return Queue{ std::move(queue), std::move(*semaphore), queueFamily };
}

auto Device::createDevice(
    const vk::raii::PhysicalDevice& pd,
    const Capabilities& capabilities,
    const QueueFamilyIndices& queueFamilyIndices)
    -> std::expected<vk::raii::Device, Error>
{
    auto features = createFeatureChain();

    std::set uniqueQueueFamilies{
        queueFamilyIndices.graphics,
        queueFamilyIndices.compute,
        queueFamilyIndices.transfer,
        queueFamilyIndices.present,
    };

    auto queuePriority = 1.0f;
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

    auto extensions = queryExtensions(capabilities);

    auto deviceInfo = vk::DeviceCreateInfo{}
        .setPNext(&features.get<vk::PhysicalDeviceFeatures2>())
        .setQueueCreateInfos(queueCreateInfos)
        .setPEnabledExtensionNames(extensions);

    auto device = pd.createDevice(deviceInfo);
    if (!device.has_value())
        return makeError(toRHI(device.result));

    return std::move(*device);
}

auto Device::queryCapabilities(const vk::raii::PhysicalDevice& pd) -> Capabilities
{
    auto features = pd.getFeatures2< //
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceMeshShaderFeaturesEXT>();
    // const auto& vk10features = features.get<vk::PhysicalDeviceFeatures2>();
    // const auto& vk11features = features.get<vk::PhysicalDeviceVulkan11Features>();
    // const auto& vk12features = features.get<vk::PhysicalDeviceVulkan12Features>();
    // const auto& vk13features = features.get<vk::PhysicalDeviceVulkan13Features>();
    const auto& meshShaderFeatures = features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();

    return Capabilities{
        .meshShaders = meshShaderFeatures.meshShader && meshShaderFeatures.taskShader,
    };
}

auto Device::queryExtensions(const Capabilities& caps) -> std::vector<const char*>
{
    std::vector extensions(requiredExtensions.begin(), requiredExtensions.end());

    if (caps.meshShaders)
    {
        extensions.emplace_back(vk::EXTMeshShaderExtensionName);
    }

    return extensions;
}

auto Device::supportsExtensions(const vk::raii::PhysicalDevice& pd) -> bool
{
    auto availableExtensions = pd.enumerateDeviceExtensionProperties();
    if (!availableExtensions.has_value())
        return false;

    return std::ranges::all_of(requiredExtensions,
        [&availableExtensions](const auto& required) {
            return std::ranges::any_of(*availableExtensions,
                [&required](const auto& available) {
                    return std::string_view{ available.extensionName } == std::string_view{ required };
                });
        });
}

auto Device::createFeatureChain() -> FeatureChain
{
    FeatureChain featureChain;

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
        .setUniformBufferStandardLayout(true)
        .setTimelineSemaphore(true);

    featureChain.get<vk::PhysicalDeviceVulkan13Features>()
        .setShaderDemoteToHelperInvocation(true)
        .setSynchronization2(true)
        .setDynamicRendering(true)
        .setMaintenance4(true);

    featureChain
        .get<vk::PhysicalDeviceShaderObjectFeaturesEXT>() //
        .setShaderObject(true);

    featureChain
        .get<vk::PhysicalDeviceMeshShaderFeaturesEXT>() //
        .setMeshShader(true)
        .setTaskShader(true);

    return featureChain;
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
