module;
#include <expected>

module aegis.rhi;
import :command_pool;
import :device;
import :error;
import :vulkan;

namespace aegis::rhi
{
auto CommandPool::create(const Desc& desc)
    -> std::expected<CommandPool, Error>
{
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = desc.queueFamily,
    };

    auto commandPool = desc.device->createCommandPool(poolInfo);
    if (!commandPool.has_value())
        return makeError(toRHI(commandPool.result));

    return CommandPool{ std::move(*commandPool) };
}

CommandPool::CommandPool(vk::raii::CommandPool pool) :
    m_commandPool{ std::move(pool) }
{
}
}
