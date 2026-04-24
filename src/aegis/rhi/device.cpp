module;
#include <vector>

module aegis.rhi;
import :device;

namespace aegis::rhi
{
Device::Device(Desc desc)
{
    createInstance();
}

void Device::createInstance()
{
    vk::ApplicationInfo appInfo{
        .pApplicationName = "aegis",
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
}
