module;
#include <expected>

module aegis.rhi;
import :command_pool;
import :device;
import :debug;
import :error;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
auto CommandPool::create(const Device& device, const Desc& desc) -> std::expected<CommandPool, Error>
{
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = desc.queueFamily,
    };

    auto commandPool = device.device().createCommandPool(poolInfo);
    if (!commandPool.has_value())
        return makeError(toRHI(commandPool.result));

    debug::setName(*device, **commandPool, desc.name);

    return CommandPool{ std::move(*commandPool) };
}

CommandPool::CommandPool(vk::raii::CommandPool pool) :
    m_commandPool{ std::move(pool) }
{
}
}
