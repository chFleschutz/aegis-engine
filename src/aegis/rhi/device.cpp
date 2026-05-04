module;
#include "vulkan/vk_platform.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cassert>
#include <format>
#include <iostream>
#include <print>
#include <set>
#include <string_view>
#include <vector>

module aegis.rhi;
import :device;

namespace aegis::rhi
{
VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
{
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
        severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
    {
        std::cerr << std::format("Vulkan Validation Error: {} \n{}\n",
            to_string(type),
            pCallbackData->pMessage);
    }
    return vk::False;
}

Device::Device(const Desc& desc)
{
    createInstance(desc);
    createDebugMessenger(desc);
    createSurface(desc);
    createPhysicalDevice(desc);
    createDevice(desc);
}

void Device::createInstance(const Desc& desc)
{
    vk::ApplicationInfo appInfo{
        .pApplicationName = desc.appName.c_str(),
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName = "Aegis Engine",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vulkanVersion,
    };

    std::vector<const char*> extensions = findExtensions();
    std::vector<const char*> layers = findLayers();

    const vk::InstanceCreateInfo instanceInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    auto [result, instance] = m_context.createInstance(instanceInfo);
    if (result != vk::Result::eSuccess)
    {
        // TODO: log error
        assert(false && "Vulkan Error: Failed to create instance");
        return;
    }
    m_instance = std::move(instance);
    std::println("Instance created");
}

void Device::createDebugMessenger(const Desc& desc)
{
    if constexpr (!enableValidation)
        return;

    constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;

    constexpr vk::DebugUtilsMessageTypeFlagsEXT messageType =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;

    constexpr vk::DebugUtilsMessengerCreateInfoEXT createInfo{ .messageSeverity = severityFlags,
        .messageType = messageType,
        .pfnUserCallback = &debugCallback };

    auto [result, debugMessenger] = m_instance.createDebugUtilsMessengerEXT(createInfo);
    if (result != vk::Result::eSuccess)
    {
        // TODO: Error
        assert(false && "Vulkan Error: Failed to create debug messenger");
        return;
    }

    m_debugMessenger = std::move(debugMessenger);
    std::println("Debug Messenger created");
}

void Device::createSurface(const Desc& desc)
{
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*m_instance, desc.window.glfwWindow(), nullptr, &surface) != 0)
    {
        assert(false && "Vulkan Error: Failed to create surface");
        return;
    }
    m_surface = vk::raii::SurfaceKHR{ m_instance, surface };

    std::println("Surface created");
}

void Device::createPhysicalDevice(const Desc& desc)
{
    auto [result, physicalDevices] = m_instance.enumeratePhysicalDevices();
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

        if (!findQueueFamilies(physicalDevice).isComplete())
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

        auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        // TODO: Check extensions

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
    vk::StructureChain<vk::DeviceCreateInfo,
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

    auto extensions = std::array{
        vk::KHRSwapchainExtensionName,
        vk::EXTMeshShaderExtensionName,
    };

    auto [graphics, present] = findQueueFamilies(m_physicalDevice);
    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueFamilies{ graphics, present };
    for (const uint32_t family : uniqueFamilies)
    {
        queueCreateInfos.emplace_back(vk::DeviceQueueCreateInfo{
            .flags = {},
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }

    vk::DeviceCreateInfo deviceInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
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

auto Device::findQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice) const
    -> QueueFamilyIndices
{
    QueueFamilyIndices indices;

    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& props = queueFamilies[i];
        const bool hasGraphics = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eGraphics);
        const bool hasCompute = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eCompute);
        const bool hasTransfer = static_cast<bool>(props.queueFlags & vk::QueueFlagBits::eTransfer);

        // TODO: Requires surface KHR extension to be loaded.
        // TODO: Most likely glfw requires it, so query glfw for extensions first
        // auto [result, hasPresent] = physicalDevice.getSurfaceSupportKHR(i, *m_surface);
        // if (result != vk::Result::eSuccess)
        //     continue;

        if (hasGraphics && hasCompute && hasTransfer && indices.graphics == vk::QueueFamilyIgnored)
            indices.graphics = i;

        // TODO: Query and enable glfw extensions (see above) assume it works for now
        // if (hasPresent && indices.present == vk::QueueFamilyIgnored)
        //     indices.present = i;
        indices.present = i;
    }

    return indices;
}

auto Device::findExtensions() const -> std::vector<const char*>
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions;
    extensions.reserve(glfwExtensionCount);
    extensions.insert(extensions.end(), glfwExtensions, glfwExtensions + glfwExtensionCount);

    if constexpr (enableValidation)
    {
        extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}

auto Device::findLayers() const -> std::vector<const char*>
{
    std::vector<const char*> layers;
    if constexpr (enableValidation)
    {
        layers.assign_range(validationLayers);
    }

    auto [result, layerProps] = m_context.enumerateInstanceLayerProperties();
    if (result != vk::Result::eSuccess)
    {
        // TODO: Error
        return {};
    }

    const auto missingIt = std::ranges::find_if(layers, [&layerProps](std::string_view required) {
        return std::ranges::none_of(layerProps, [required](const auto& provided) {
            return required == std::string_view{ provided.layerName };
        });
    });

    if (missingIt != layers.end())
    {
        // TODO: Error
        return {};
    }

    return layers;
}
}
