module;
#include "vulkan/vk_platform.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <print>

module aegis.rhi;
import :context;

namespace aegis::rhi
{
VKAPI_ATTR auto VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*) -> vk::Bool32
{
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
        severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
    {
        std::println(
            "Vulkan Validation Error: {} \n{}\n",
            to_string(type),
            pCallbackData->pMessage);
    }
    return vk::False;
}


Context::Context(const Desc& desc)
{
    createInstance(desc);
    createDebugMessenger();
    createSurface(desc);
}

auto Context::createInstance(const Desc& desc) -> void
{
    vk::ApplicationInfo appInfo{
        .pApplicationName = desc.appName.c_str(),
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName = "Aegis Engine",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vk::makeApiVersion(0, 1, 3, 0),
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
        // TODO: Error
        return;
    }

    m_instance = std::move(instance);
    std::println("Instance created");
}

auto Context::createDebugMessenger() -> void
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
        return;
    }

    m_debugMessenger = std::move(debugMessenger);
    std::println("Debug Messenger created");
}

auto Context::createSurface(const Desc& desc) -> void
{
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(*m_instance, desc.window.glfwWindow(), nullptr, &surface) != 0)
    {
        // TODO: Error
        return;
    }
    m_surface = vk::raii::SurfaceKHR{ m_instance, surface };
    std::println("Surface created");
}

auto Context::findExtensions() -> std::vector<const char*>
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions{ glfwExtensions, glfwExtensions + glfwExtensionCount };

    if constexpr (enableValidation)
    {
        extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}

auto Context::findLayers() const -> std::vector<const char*>
{
    std::vector<const char*> layers;
    if constexpr (enableValidation)
    {
        layers.emplace_back("VK_LAYER_KHRONOS_validation");
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
