module;
#include <expected>

module aegis.rhi;
import :command_buffer;

namespace aegis::rhi
{
auto CommandBuffer::create(const Desc& desc)
    -> std::expected<CommandBuffer, Error>
{
    vk::CommandBufferAllocateInfo info{
        .commandPool = desc.pool.commandPool(),
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };

    auto commandBuffer = desc.device.device().allocateCommandBuffers(info);
    if (!commandBuffer.has_value())
        return vkError(commandBuffer.result, "Failed to allocate command buffer");

    return CommandBuffer{ std::move(commandBuffer->front()) };
}

CommandBuffer::CommandBuffer(vk::raii::CommandBuffer cmdBuffer) :
    m_commandBuffer{ std::move(cmdBuffer) }
{
}
}
