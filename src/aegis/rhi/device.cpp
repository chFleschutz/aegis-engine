module;
#include <vector>

module aegis.rhi;
import :device;

namespace aegis::rhi
{
Device::Device(const Desc& desc)
{
    createInstance(desc);
    m_surface = vk::raii::SurfaceKHR{ m_instance, desc.createSurface(*m_instance) };
    createPhysicalDevice(desc);
}

void Device::createInstance(const Desc& desc)
{
    vk::ApplicationInfo appInfo{
        .pApplicationName = desc.appName.c_str(),
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName = "Aegis Engine",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vk::makeApiVersion(1, 3, 0, 0),
    };

    std::vector<char*> extensions;

    vk::InstanceCreateInfo instanceInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    auto instanceRes = m_context.createInstance(instanceInfo);
    if (instanceRes.result != vk::Result::eSuccess)
    {
        // TODO: log error
        return;
    }
    m_instance = std::move(instanceRes.value);
}

void Device::createPhysicalDevice(const Desc& desc)
{
    auto [result, physicalDevices] = m_instance.enumeratePhysicalDevices();
    if (result != vk::Result::eSuccess || physicalDevices.empty())
    {
        // TODO: log error
        return;
    }

    for (auto& physicalDevice : physicalDevices)
    {
        auto featureChain = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceMeshShaderFeaturesEXT>();

        const auto& vk10features = featureChain.get<vk::PhysicalDeviceFeatures2>();
        const auto& vk11features = featureChain.get<vk::PhysicalDeviceVulkan11Features>();
        const auto& vk12features = featureChain.get<vk::PhysicalDeviceVulkan12Features>();
        const auto& vk13features = featureChain.get<vk::PhysicalDeviceVulkan13Features>();
        const auto& meshShaderFeatures = featureChain.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();

        if (!vk10features.features.samplerAnisotropy)
            continue;

        if (!vk11features.shaderDrawParameters)
            continue;

        // Bindless
        if (!vk12features.descriptorIndexing || !vk12features.shaderUniformBufferArrayNonUniformIndexing ||
            !vk12features.shaderSampledImageArrayNonUniformIndexing ||
            !vk12features.shaderStorageBufferArrayNonUniformIndexing ||
            !vk12features.shaderStorageImageArrayNonUniformIndexing ||
            !vk12features.descriptorBindingUniformBufferUpdateAfterBind ||
            !vk12features.descriptorBindingSampledImageUpdateAfterBind ||
            !vk12features.descriptorBindingStorageImageUpdateAfterBind ||
            !vk12features.descriptorBindingStorageBufferUpdateAfterBind || !vk12features.descriptorBindingPartiallyBound ||
            !vk12features.descriptorBindingVariableDescriptorCount || !vk12features.runtimeDescriptorArray)
            continue;

        // Buffer layouts
        if (!vk12features.uniformBufferStandardLayout || !vk12features.scalarBlockLayout)
            continue;

        // 8-bit storage
        if (!vk12features.shaderInt8 || !vk12features.storageBuffer8BitAccess ||
            !vk12features.uniformAndStorageBuffer8BitAccess || !vk12features.storagePushConstant8)
            continue;

        if (!vk13features.dynamicRendering || !vk13features.maintenance4 || !vk13features.shaderDemoteToHelperInvocation)
            continue;

        if (!meshShaderFeatures.meshShader || !meshShaderFeatures.taskShader)
            continue;

        auto propsChain =
            physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan11Properties, vk::PhysicalDeviceVulkan12Properties, vk::PhysicalDeviceVulkan13Properties>();

        const auto& coreProps = propsChain.get<vk::PhysicalDeviceProperties2>();
        const auto& vk11Props = propsChain.get<vk::PhysicalDeviceVulkan11Properties>();
        const auto& vk12Props = propsChain.get<vk::PhysicalDeviceVulkan12Properties>();
        const auto& vk13Props = propsChain.get<vk::PhysicalDeviceVulkan13Properties>();

        m_properties = Properties{
            .core = coreProps,
            .vk11 = vk11Props,
            .vk12 = vk12Props,
            .vk13 = vk13Props,
        };

        m_capabilities =
            Capabilities{ .meshShaders = meshShaderFeatures.meshShader && meshShaderFeatures.taskShader };

        // TODO: Maybe select based on some score instead of just using the first
        // Also prefer discrete gpu (Here its possible to end up with integrated even if discrete is present)
        m_physicalDevice = physicalDevice;
        break;
    }
}
}
