module;
#include <cassert>
#include <expected>
#include <ranges>
#include <vector>

module aegis.rhi;
import :command_buffer;
import :command_pool;
import :device;
import :error;
import :vulkan;

namespace aegis::rhi
{
auto CommandBuffer::create(const Desc& desc) -> std::expected<CommandBuffer, Error>
{
    vk::CommandBufferAllocateInfo info{
        .commandPool = desc.pool.commandPool(),
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };

    auto commandBuffer = desc.device->allocateCommandBuffers(info);
    if (!commandBuffer.has_value())
        return makeError(toRHI(commandBuffer.result));

    return CommandBuffer{ std::move(commandBuffer->front()) };
}

auto CommandBuffer::begin() const -> void
{
    auto result = m_commandBuffer.begin({});
    assert(result == vk::Result::eSuccess && "Failed to begin command buffer");
}

auto CommandBuffer::end() const -> void
{
    auto result = m_commandBuffer.end();
    assert(result == vk::Result::eSuccess && "Failed to end command buffer");
}

auto CommandBuffer::beginRendering(const RenderingDesc& desc) const -> void
{
    auto colorAttachments = desc.attachments
                            | std::views::transform([](const auto& a) {
                                return vk::RenderingAttachmentInfo{
                                    .imageView = a.imageView,
                                    .imageLayout = a.imageLayout,
                                    .loadOp = vk::AttachmentLoadOp::eClear,
                                    .storeOp = vk::AttachmentStoreOp::eStore,
                                    .clearValue = a.clearValue,
                                };
                            })
                            | std::ranges::to<std::vector>();

    vk::RenderingInfo renderingInfo{
        .renderArea = vk::Rect2D{
            .offset = { 0, 0 },
            .extent = { desc.extent.x, desc.extent.y },
        },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = static_cast<std::uint32_t>(colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr,
    };

    m_commandBuffer.beginRendering(renderingInfo);
}

auto CommandBuffer::endRendering() const -> void
{
    m_commandBuffer.endRendering();
}

auto CommandBuffer::bindPipeline(const Pipeline& pipeline) const -> void
{
    m_commandBuffer.bindPipeline(pipeline.bindPoint(), pipeline.pipeline());
}

auto CommandBuffer::setViewport(std::uint32_t width, std::uint32_t height) const -> void
{
    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    m_commandBuffer.setViewport(0, viewport);
}

auto CommandBuffer::setScissor(std::uint32_t width, std::uint32_t height) const -> void
{
    vk::Rect2D scissor{
        .offset = { 0, 0 },
        .extent = { width, height },
    };
    m_commandBuffer.setScissor(0, scissor);
}

auto CommandBuffer::draw(std::uint32_t vertexCount) const -> void
{
    m_commandBuffer.draw(vertexCount, 1, 0, 0);
}

auto CommandBuffer::transitionImageLayout(const ImageLayoutTransition& cmd) const -> void
{
    auto src = toVulkan(cmd.oldState);
    auto dst = toVulkan(cmd.newState);

    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src.stageMask,
        .srcAccessMask = src.accessMask,
        .dstStageMask = dst.stageMask,
        .dstAccessMask = dst.accessMask,
        .oldLayout = src.layout,
        .newLayout = dst.layout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = cmd.image,
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = cmd.aspectFlags,
            .baseMipLevel = cmd.baseMipLevel,
            .levelCount = cmd.levelCount,
            .baseArrayLayer = cmd.baseArrayLayer,
            .layerCount = cmd.layerCount,
        },
    };

    vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    m_commandBuffer.pipelineBarrier2(dependencyInfo);
}

CommandBuffer::CommandBuffer(vk::raii::CommandBuffer cmdBuffer) :
    m_commandBuffer{ std::move(cmdBuffer) }
{
}
}
