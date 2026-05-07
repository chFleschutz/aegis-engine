module;
#include "vulkan/vk_platform.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <expected>
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


auto Context::create(const Desc& desc) -> std::expected<Context, Error>
{
    auto context = vk::raii::Context{};

    auto instance = createInstance(context, desc);
    if (!instance)
        return std::unexpected{ std::move(instance.error()) };

    auto messenger = createDebugMessenger(*instance);
    if (!messenger)
        return std::unexpected{ std::move(messenger.error()) };

    auto surface = createSurface(*instance, desc);
    if (!surface)
        return std::unexpected{ std::move(surface.error()) };

    return Context{ std::move(context),
                    std::move(*instance),
                    std::move(*messenger),
                    std::move(*surface) };
}

Context::Context(
    vk::raii::Context context,
    vk::raii::Instance instance,
    vk::raii::DebugUtilsMessengerEXT messenger,
    vk::raii::SurfaceKHR surface) :
    m_context{ std::move(context) },
    m_instance{ std::move(instance) },
    m_debugMessenger{ std::move(messenger) },
    m_surface{ std::move(surface) }
{
}

auto Context::createInstance(const vk::raii::Context& context, const Desc& desc)
    -> std::expected<vk::raii::Instance, Error>
{
    vk::ApplicationInfo appInfo{
        .pApplicationName = desc.appName.c_str(),
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName = "Aegis Engine",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vk::makeApiVersion(0, 1, 3, 0),
    };

    auto extensions = findExtensions();
    auto layers = findLayers(context);
    if (!layers)
        return std::unexpected{ std::move(layers.error()) };

    const vk::InstanceCreateInfo instanceInfo{
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers->size()),
        .ppEnabledLayerNames = layers->data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    auto [result, instance] = context.createInstance(instanceInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create instance");

    return { std::move(instance) };
}

auto Context::createDebugMessenger(const vk::raii::Instance& instance)
    -> std::expected<vk::raii::DebugUtilsMessengerEXT, Error>
{
    if constexpr (!enableValidation)
        return vk::raii::DebugUtilsMessengerEXT{ nullptr };

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

    auto [result, debugMessenger] = instance.createDebugUtilsMessengerEXT(createInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create debug messenger");

    return { std::move(debugMessenger) };
}

auto Context::createSurface(const vk::raii::Instance& instance, const Desc& desc)
    -> std::expected<vk::raii::SurfaceKHR, Error>
{
    VkSurfaceKHR surface;
    auto result = static_cast<vk::Result>(
        glfwCreateWindowSurface(*instance, desc.window.glfwWindow(), nullptr, &surface));
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create Surface");

    return vk::raii::SurfaceKHR{ instance, surface };
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
auto Context::findLayers(const vk::raii::Context& context)
    -> std::expected<std::vector<const char*>, Error>
{
    std::vector<const char*> layers;
    if constexpr (enableValidation)
    {
        layers.emplace_back("VK_LAYER_KHRONOS_validation");
    }

    auto [result, layerProps] = context.enumerateInstanceLayerProperties();
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to enumerate instance layer properties");

    const auto missingIt = std::ranges::find_if(layers, [&layerProps](std::string_view required) {
        return std::ranges::none_of(layerProps, [required](const auto& provided) {
            return required == std::string_view{ provided.layerName };
        });
    });

    if (missingIt != layers.end())
        return vkError(vk::Result::eErrorUnknown, "Missing required instance layer");

    return layers;
}
}
