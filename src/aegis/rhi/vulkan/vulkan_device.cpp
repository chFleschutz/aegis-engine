module;
#include <memory>

module aegis.rhi.vulkan;
import :device;

namespace aegis::rhi::vulkan
{
Device::Device(const rhi::Device::Desc& desc)
{
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
}
