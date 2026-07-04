module;
#include <algorithm>
#include <cassert>
#include <expected>
#include <optional>
#include <ranges>
#include <vector>

module aegis.rhi;
import :command_buffer;
import :command_pool;
import :context;
import :debug;
import :device;
import :error;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
auto Attachment::color(ImageViewHandle view, ClearColor clear) -> Attachment
{
    return Attachment{
        .imageView = view,
        .loadOp = AttachmentLoadOp::Clear,
        .storeOp = AttachmentStoreOp::Store,
        .clearValue = clear,
    };
}

auto Attachment::colorLoad(ImageViewHandle view) -> Attachment
{
    return Attachment{
        .imageView = view,
        .loadOp = AttachmentLoadOp::Load,
        .storeOp = AttachmentStoreOp::Store,
    };
}

auto Attachment::depth(ImageViewHandle view, ClearDepthStencil clear) -> Attachment
{
    return Attachment{
        .imageView = view,
        .loadOp = AttachmentLoadOp::Clear,
        .storeOp = AttachmentStoreOp::Store,
        .clearValue = clear,
    };
}

auto Attachment::depthReadOnly(ImageViewHandle view) -> Attachment
{
    return Attachment{
        .imageView = view,
        .loadOp = AttachmentLoadOp::Load,
        .storeOp = AttachmentStoreOp::None,
    };
}

auto CommandBuffer::begin(bool oneTimeSubmit) const -> void
{
    [[maybe_unused]] auto result = m_commandBuffer.begin({
        .flags = oneTimeSubmit
                     ? vk::CommandBufferUsageFlagBits::eOneTimeSubmit
                     : vk::CommandBufferUsageFlags{},
    });
    assert(result == vk::Result::eSuccess && "Failed to begin command buffer");
}

auto CommandBuffer::end() const -> void
{
    [[maybe_unused]] auto result = m_commandBuffer.end();
    assert(result == vk::Result::eSuccess && "Failed to end command buffer");
}

auto CommandBuffer::beginRendering(const RenderingCmd& desc) const -> void
{
    assert(desc.colorAttachments.size() < maxColorAttachments);

    auto transformAttachment = [&](const auto& attachment) -> vk::RenderingAttachmentInfo {
        const auto& view = m_device.get(attachment.imageView);
        return vk::RenderingAttachmentInfo{
            .imageView = view.vk(),
            .imageLayout = deriveAttachmentImageLayout(attachment.storeOp),
            .loadOp = toVulkan(attachment.loadOp),
            .storeOp = toVulkan(attachment.storeOp),
            .clearValue = (attachment.loadOp == AttachmentLoadOp::Clear && attachment.clearValue)
                              ? toVulkan(*attachment.clearValue)
                              : vk::ClearValue{},
        };
    };

    std::array<vk::RenderingAttachmentInfo, maxColorAttachments> colorAttachments;
    std::ranges::transform(desc.colorAttachments, colorAttachments.begin(), transformAttachment);

    auto depthAttachment = desc.depthAttachment.transform(transformAttachment);

    vk::RenderingInfo renderingInfo{
        .renderArea = vk::Rect2D{ vk::Offset2D{ 0, 0 }, deriveExtent(desc) },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = static_cast<std::uint32_t>(desc.colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = depthAttachment ? &depthAttachment.value() : nullptr,
        .pStencilAttachment = nullptr,
    };

    m_commandBuffer.beginRendering(renderingInfo);
}

auto CommandBuffer::endRendering() const -> void
{
    m_commandBuffer.endRendering();
}

auto CommandBuffer::insertLabel(std::string_view name, std::array<float, 4> color) const -> void
{
    if constexpr (Context::enableValidation)
    {
        vk::DebugUtilsLabelEXT label{
            .pLabelName = name.data(),
            .color = color,
        };
        m_commandBuffer.insertDebugUtilsLabelEXT(label);
    }
}

auto CommandBuffer::beginLabel(std::string_view name, std::array<float, 4> color) const -> void
{
    if constexpr (Context::enableValidation)
    {
        vk::DebugUtilsLabelEXT label{
            .pLabelName = name.data(),
            .color = color,
        };
        m_commandBuffer.beginDebugUtilsLabelEXT(label);
    }
}

auto CommandBuffer::endLabel() const -> void
{
    if constexpr (Context::enableValidation)
    {
        m_commandBuffer.endDebugUtilsLabelEXT();
    }
}

auto CommandBuffer::bindPipeline(const Pipeline& pipeline) const -> void
{
    m_commandBuffer.bindPipeline(pipeline.bindPoint(), *pipeline);
}

auto CommandBuffer::setViewport(Extent2D extent) const -> void
{
    vk::Viewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.x),
        .height = static_cast<float>(extent.y),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    m_commandBuffer.setViewport(0, viewport);
}

auto CommandBuffer::setScissor(Extent2D extent) const -> void
{
    vk::Rect2D scissor{
        .offset = { 0, 0 },
        .extent = { extent.x, extent.y },
    };
    m_commandBuffer.setScissor(0, scissor);
}

auto CommandBuffer::draw(std::uint32_t vertexCount) const -> void
{
    m_commandBuffer.draw(vertexCount, 1, 0, 0);
}

auto CommandBuffer::transitionImageLayout(ImageViewHandle imageViewHandle, ResourceState oldState,
    ResourceState newState) const -> void
{
    auto& view = m_device.get(imageViewHandle);
    auto [srcLayout, srcStage, srcAccess] = toVulkan(oldState);
    auto [dstLayout, dstStage, dstAccess] = toVulkan(newState);

    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = srcLayout,
        .newLayout = dstLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = view.vkImage(),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(view.format()),
            .baseMipLevel = view.range().baseMipLevel,
            .levelCount = view.range().mipLevelCount,
            .baseArrayLayer = view.range().baseArrayLayer,
            .layerCount = view.range().arrayLayerCount,
        },
    };

    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

auto CommandBuffer::transitionImageLayout(ImageHandle imageHandle, ResourceState oldState,
    ResourceState newState) const -> void
{
    auto& image = m_device.get(imageHandle);
    auto [srcLayout, srcStage, srcAccess] = toVulkan(oldState);
    auto [dstLayout, dstStage, dstAccess] = toVulkan(newState);

    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = srcLayout,
        .newLayout = dstLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image.vk(),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(image.format()),
            .baseMipLevel = 0,
            .levelCount = image.arrayLayers(),
            .baseArrayLayer = 0,
            .layerCount = image.arrayLayers(),
        },
    };

    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        }
    );
}

auto CommandBuffer::generateMipmaps(ImageHandle imageHandle, ResourceState currentState) const
    -> void
{
    if (currentState != ResourceState::CopyDst)
        transitionImageLayout(imageHandle, currentState, ResourceState::CopyDst);

    auto& image = m_device.get(imageHandle);
    auto aspectMask = deriveImageAspectFlags(image.format());
    auto [srcLayout, srcStage, srcAccess] = toVulkan(ResourceState::CopyDst);
    auto [dstLayout, dstStage, dstAccess] = toVulkan(ResourceState::CopySrc);
    vk::ImageMemoryBarrier2 mipToTransferSrcBarrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = srcLayout,
        .newLayout = dstLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image.vk(),
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = aspectMask,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = image.arrayLayers(),
        }
    };

    vk::ImageBlit2 blit{
        .srcSubresource = vk::ImageSubresourceLayers{
            .aspectMask = aspectMask,
            .baseArrayLayer = 0,
            .layerCount = image.arrayLayers(),
        },
        .dstSubresource = vk::ImageSubresourceLayers{
            .aspectMask = aspectMask,
            .baseArrayLayer = 0,
            .layerCount = image.arrayLayers(),
        },
    };

    for (uint32_t i = 1; i < image.mipLevels(); ++i)
    {
        mipToTransferSrcBarrier.subresourceRange.baseMipLevel = i - 1;
        m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &mipToTransferSrcBarrier,
        });

        blit.srcSubresource.mipLevel = i - 1;
        blit.srcOffsets = std::array{
            vk::Offset3D{ 0, 0, 0 },
            vk::Offset3D{
                .x = std::max(1, static_cast<std::int32_t>(image.extent().x >> (i - 1))),
                .y = std::max(1, static_cast<std::int32_t>(image.extent().y >> (i - 1))),
                .z = std::max(1, static_cast<std::int32_t>(image.extent().z >> (i - 1))),
            }
        };
        blit.dstSubresource.mipLevel = i;
        blit.dstOffsets = std::array{
            vk::Offset3D{ 0, 0, 0 },
            vk::Offset3D{
                .x = std::max(1, static_cast<std::int32_t>(image.extent().x >> i)),
                .y = std::max(1, static_cast<std::int32_t>(image.extent().y >> i)),
                .z = std::max(1, static_cast<std::int32_t>(image.extent().z >> i)),
            }
        };

        m_commandBuffer.blitImage2(vk::BlitImageInfo2{
            .srcImage = image.vk(),
            .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
            .dstImage = image.vk(),
            .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
            .regionCount = 1,
            .pRegions = &blit,
            .filter = vk::Filter::eLinear,
        });
    }

    // Transition last level so the image is fully in transfer src optimal layout
    mipToTransferSrcBarrier.subresourceRange.baseMipLevel = image.mipLevels() - 1;
    mipToTransferSrcBarrier.dstStageMask = vk::PipelineStageFlagBits2::eNone;
    mipToTransferSrcBarrier.dstAccessMask = vk::AccessFlagBits2::eNone;
    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &mipToTransferSrcBarrier,
    });
}

auto CommandBuffer::copyBuffer(const Buffer& src, const Buffer& dst, std::size_t size, std::size_t srcOffset,
    std::size_t dstOffset) const -> void
{
    vk::BufferCopy2 copyRegion{
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = size,
    };

    m_commandBuffer.copyBuffer2(vk::CopyBufferInfo2{
        .srcBuffer = src.vk(),
        .dstBuffer = dst.vk(),
        .regionCount = 1,
        .pRegions = &copyRegion,
    });
}

auto CommandBuffer::create(const Device& device, const Desc& desc) -> std::expected<CommandBuffer, Error>
{
    vk::CommandBufferAllocateInfo info{
        .commandPool = *desc.pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };

    auto commandBuffer = device.device().allocateCommandBuffers(info);
    if (!commandBuffer.has_value())
        return makeError(toRHI(commandBuffer.result));

    debug::setName(*device, *commandBuffer->front(), desc.name);

    return CommandBuffer{ device, std::move(commandBuffer->front()) };
}

CommandBuffer::CommandBuffer(const Device& device, vk::raii::CommandBuffer cmdBuffer) :
    m_device{ device },
    m_commandBuffer{ std::move(cmdBuffer) }
{
}

auto CommandBuffer::deriveExtent(const RenderingCmd& cmd) const -> vk::Extent2D
{
    if (!cmd.colorAttachments.empty())
    {
        const auto& image = m_device.get(cmd.colorAttachments.front().imageView);
        return vk::Extent2D{ image.extent().x, image.extent().y };
    }

    if (cmd.depthAttachment)
    {
        const auto& image = m_device.get(cmd.depthAttachment->imageView);
        return vk::Extent2D{ image.extent().x, image.extent().y };
    }

    assert(false && "RenderingCmd has no attachments");
    std::unreachable();
}
}
