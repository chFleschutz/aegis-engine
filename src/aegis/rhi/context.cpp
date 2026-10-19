module;
#include "vulkan/vk_platform.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

module aegis.rhi;
import std;
import :context;
import :error;
import :vulkan_conversions;
import aegis.base;
import aegis.platform;

namespace aegis::rhi
{
namespace
{
std::atomic<std::uint32_t> g_validationErrors{ 0 };
}

VKAPI_ATTR auto VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*)
    -> vk::Bool32
{
    if (severity != vk::DebugUtilsMessageSeverityFlagBitsEXT::eError &&
        severity != vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
        return vk::False;

    auto typeStr = vk::to_string(type);
    std::string_view trimmedType = typeStr;
    if (trimmedType.size() >= 4)
    {
        trimmedType.remove_prefix(2);
        trimmedType.remove_suffix(2);
    }

    const bool isError = severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    if (isError)
        ++g_validationErrors;

    if (isError)
        log::error("Vulkan {}: {}", trimmedType, pCallbackData->pMessage);
    else
        log::warn("Vulkan {}: {}", trimmedType, pCallbackData->pMessage);
    return vk::False;
}

auto Context::validationErrorCount() noexcept -> std::uint32_t
{
    return g_validationErrors.load();
}

auto Context::create(const Desc& desc) -> std::expected<std::unique_ptr<Context>, Error>
{
    vk::raii::Context context{};

    auto instance = createInstance(context, desc);
    if (!instance)
        return std::unexpected{ instance.error() };

    auto messenger = createDebugMessenger(*instance);
    if (!messenger)
        return std::unexpected{ messenger.error() };

    auto surface = createSurface(*instance, desc);
    if (!surface)
        return std::unexpected{ surface.error() };

    return std::make_unique<Context>(
        std::move(context),
        std::move(*instance),
        std::move(*messenger),
        std::move(*surface));
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

auto Context::createDevice(const Device::Desc& desc) const -> std::expected<std::unique_ptr<Device>, Error>
{
    return Device::create(m_instance, m_surface, desc);
}

auto Context::createInstance(
    const vk::raii::Context& context,
    const Desc& desc)
    -> std::expected<vk::raii::Instance, Error>
{
    const std::string appName{ desc.appName }; // Vulkan reads a null-terminated string
    vk::ApplicationInfo appInfo{
        .pApplicationName = appName.c_str(),
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName = "Aegis Engine",
        .engineVersion = vk::makeVersion(1, 0, 0),
        .apiVersion = vulkanVersion,
    };

    auto extensions = findExtensions(desc);
    auto layers = findLayers(context);
    if (!layers)
        return std::unexpected{ layers.error() };

    // Synchronization validation is off by default in the layer, and without it barrier bugs pass
    // validation silently.
    constexpr vk::Bool32 enableSyncValidation{ vk::True };
    const std::array layerSettings{
        vk::LayerSettingEXT{
            .pLayerName = "VK_LAYER_KHRONOS_validation",
            .pSettingName = "validate_sync",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enableSyncValidation,
        },
    };
    const vk::LayerSettingsCreateInfoEXT layerSettingsInfo{
        .settingCount = static_cast<std::uint32_t>(layerSettings.size()),
        .pSettings = layerSettings.data(),
    };

    const vk::InstanceCreateInfo instanceInfo{
        .pNext = enableValidation ? &layerSettingsInfo : nullptr,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<std::uint32_t>(layers->size()),
        .ppEnabledLayerNames = layers->data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    auto instance = context.createInstance(instanceInfo);
    if (!instance.has_value())
        return makeError(toRHI(instance.error()));

    return std::move(*instance);
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

    constexpr vk::DebugUtilsMessengerCreateInfoEXT createInfo{
        .messageSeverity = severityFlags,
        .messageType = messageType,
        .pfnUserCallback = &debugCallback,
    };

    auto debugMessenger = instance.createDebugUtilsMessengerEXT(createInfo);
    if (!debugMessenger.has_value())
        return makeError(toRHI(debugMessenger.error()));

    return std::move(*debugMessenger);
}

auto Context::createSurface(
    const vk::raii::Instance& instance,
    const Desc& desc)
    -> std::expected<vk::raii::SurfaceKHR, Error>
{
    if (!desc.window)
        return vk::raii::SurfaceKHR{ nullptr };

    VkSurfaceKHR surface;
    auto result = static_cast<vk::Result>(
        glfwCreateWindowSurface(*instance, desc.window->glfwWindow(), nullptr, &surface));
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return vk::raii::SurfaceKHR{ instance, surface };
}

auto Context::findExtensions(const Desc& desc) -> std::vector<const char*>
{
    std::vector<const char*> extensions;

    // Headless never touches GLFW: it may not even be initialized, since only a Window does that.
    if (desc.window)
    {
        std::uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        extensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    if constexpr (enableValidation)
    {
        extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
        extensions.emplace_back(vk::EXTLayerSettingsExtensionName);
    }

    return extensions;
}

auto Context::findLayers(const vk::raii::Context& context) -> std::expected<std::vector<const char*>, Error>
{
    std::vector<const char*> layers;
    if constexpr (enableValidation)
    {
        layers.emplace_back("VK_LAYER_KHRONOS_validation");
    }

    auto layerProps = context.enumerateInstanceLayerProperties();
    if (!layerProps.has_value())
        return makeError(toRHI(layerProps.error()));

    const auto missingIt = std::ranges::find_if(layers,
        [&layerProps](std::string_view required) {
            return std::ranges::none_of(*layerProps,
                [required](const auto& provided) {
                    return required == std::string_view{ provided.layerName };
                });
        });

    if (missingIt != layers.end())
        return makeError(ErrorCode::Unknown);

    return layers;
}
}
