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

namespace aegis::rhi
{
auto Attachment::color(const ImageRef& image, ClearColor clear) -> Attachment
{
    return Attachment{
        .image = image,
        .loadOp = AttachmentLoadOp::Clear,
        .storeOp = AttachmentStoreOp::Store,
        .clearValue = clear,
    };
}

auto Attachment::colorLoad(const ImageRef& image) -> Attachment
{
    return Attachment{
        .image = image,
        .loadOp = AttachmentLoadOp::Load,
        .storeOp = AttachmentStoreOp::Store,
    };
}

auto Attachment::depth(const ImageRef& image, ClearDepthStencil clear) -> Attachment
{
    return Attachment{
        .image = image,
        .loadOp = AttachmentLoadOp::Clear,
        .storeOp = AttachmentStoreOp::Store,
        .clearValue = clear,
    };
}

auto Attachment::depthReadOnly(const ImageRef& image) -> Attachment
{
    return Attachment{
        .image = image,
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

    std::array<vk::RenderingAttachmentInfo, maxColorAttachments> colorAttachments;
    std::ranges::transform(desc.colorAttachments,
        colorAttachments.begin(),
        [](const auto& attachment) -> vk::RenderingAttachmentInfo {
            return toVulkan(attachment);
        });

    auto depthAttachment = desc.depthAttachment
                               ? std::optional(toVulkan(*desc.depthAttachment))
                               : vk::RenderingAttachmentInfo{};

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

auto CommandBuffer::transitionImageLayout(const ImageLayoutTransition& cmd) const -> void
{
    auto [srcLayout, srcStage, srcAccess] = toVulkan(cmd.oldState);
    auto [dstLayout, dstStage, dstAccess] = toVulkan(cmd.newState);

    vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = srcLayout,
        .newLayout = dstLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = cmd.imageRef.image,
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = deriveImageAspectFlags(cmd.imageRef.format),
            .baseMipLevel = cmd.imageRef.baseMipLevel,
            .levelCount = cmd.imageRef.levelCount,
            .baseArrayLayer = cmd.imageRef.baseArrayLayer,
            .layerCount = cmd.imageRef.layerCount,
        },
    };

    vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    m_commandBuffer.pipelineBarrier2(dependencyInfo);
}

auto CommandBuffer::generateMipmaps(const ImageRef& image, ResourceState currentState) const -> void
{
    if (currentState != ResourceState::CopyDst)
    {
        transitionImageLayout({
            .imageRef = image,
            .oldState = currentState,
            .newState = ResourceState::CopyDst,
        });
    }

    auto aspectMask = deriveImageAspectFlags(image.format);
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
        .image = image.image,
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = aspectMask,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = image.layerCount,
        }
    };

    vk::ImageBlit2 blit{
        .srcSubresource = vk::ImageSubresourceLayers{
            .aspectMask = aspectMask,
            .baseArrayLayer = 0,
            .layerCount = image.layerCount,
        },
        .dstSubresource = vk::ImageSubresourceLayers{
            .aspectMask = aspectMask,
            .baseArrayLayer = 0,
            .layerCount = image.layerCount,
        },
    };

    for (uint32_t i = 1; i < image.levelCount; ++i)
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
                .x = std::max(1, static_cast<std::int32_t>(image.extent.x >> (i - 1))),
                .y = std::max(1, static_cast<std::int32_t>(image.extent.y >> (i - 1))),
                .z = std::max(1, static_cast<std::int32_t>(image.extent.z >> (i - 1))),
            }
        };
        blit.dstSubresource.mipLevel = i;
        blit.dstOffsets = std::array{
            vk::Offset3D{ 0, 0, 0 },
            vk::Offset3D{
                .x = std::max(1, static_cast<std::int32_t>(image.extent.x >> i)),
                .y = std::max(1, static_cast<std::int32_t>(image.extent.y >> i)),
                .z = std::max(1, static_cast<std::int32_t>(image.extent.z >> i)),
            }
        };

        m_commandBuffer.blitImage2(vk::BlitImageInfo2{
            .srcImage = image.image,
            .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
            .dstImage = image.image,
            .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
            .regionCount = 1,
            .pRegions = &blit,
            .filter = vk::Filter::eLinear,
        });
    }

    // Transition last level so the image is fully in transfer src optimal layout
    mipToTransferSrcBarrier.subresourceRange.baseMipLevel = image.levelCount - 1;
    mipToTransferSrcBarrier.dstStageMask = vk::PipelineStageFlagBits2::eNone;
    mipToTransferSrcBarrier.dstAccessMask = vk::AccessFlagBits2::eNone;
    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &mipToTransferSrcBarrier,
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

    return CommandBuffer{ std::move(commandBuffer->front()) };
}

auto CommandBuffer::deriveExtent(const RenderingCmd& cmd) -> vk::Extent2D
{
    if (!cmd.colorAttachments.empty())
        return vk::Extent2D{
            cmd.colorAttachments[0].image.extent.x,
            cmd.colorAttachments[0].image.extent.y
        };
    if (cmd.depthAttachment)
        return vk::Extent2D{
            cmd.depthAttachment->image.extent.x,
            cmd.depthAttachment->image.extent.y
        };
    assert(false && "RenderingCmd has no attachments");
    return {};
}

CommandBuffer::CommandBuffer(vk::raii::CommandBuffer cmdBuffer) :
    m_commandBuffer{ std::move(cmdBuffer) }
{
}
}
