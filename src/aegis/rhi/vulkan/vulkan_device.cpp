module;
#include <memory>
#include <vector>

module aegis.rhi.vulkan;
import :device;
import :core;

namespace aegis::rhi::vulkan
{
Device::Device(const rhi::Device::Desc& desc)
{
    createInstance();

}

auto Device::createBuffer(const Buffer::Desc& desc) -> std::unique_ptr<Buffer>
{
    return nullptr;
}

auto Device::createTexture(const Buffer::Desc& desc) -> std::unique_ptr<Texture>
{
    return nullptr;
}

auto Device::createPipeline(const Buffer::Desc& desc) -> std::unique_ptr<Pipeline>
{
    return nullptr;
}

auto Device::createCommandBuffer(const CommandBuffer::Desc& desc) -> std::unique_ptr<CommandBuffer>
{
    return nullptr;
}

void Device::submit(const CommandBuffer& cmd)
{
}

void Device::beginFrame()
{
}

void Device::endFrame()
{
}

void Device::waitIdle()
{
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
