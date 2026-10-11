module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
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
    m_graphicsSetBound = false;
    m_computeSetBound = false;

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
    assert(desc.colorAttachments.size() <= maxColorAttachments);

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
    auto stencilAttachment = desc.stencilAttachment.transform(transformAttachment);

    vk::RenderingInfo renderingInfo{
        .renderArea = vk::Rect2D{ vk::Offset2D{ 0, 0 }, deriveExtent(desc) },
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = static_cast<std::uint32_t>(desc.colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = depthAttachment ? &depthAttachment.value() : nullptr,
        .pStencilAttachment = stencilAttachment ? &stencilAttachment.value() : nullptr,
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
        const std::string terminated{ name }; // Vulkan reads a null-terminated string
        vk::DebugUtilsLabelEXT label{
            .pLabelName = terminated.c_str(),
            .color = color,
        };
        m_commandBuffer.insertDebugUtilsLabelEXT(label);
    }
}

auto CommandBuffer::beginLabel(std::string_view name, std::array<float, 4> color) const -> void
{
    if constexpr (Context::enableValidation)
    {
        const std::string terminated{ name }; // Vulkan reads a null-terminated string
        vk::DebugUtilsLabelEXT label{
            .pLabelName = terminated.c_str(),
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

    // Every pipeline shares one layout, so a bound set stays valid across pipeline changes: binding it
    // once per bind point and recording is enough.
    auto& setBound = pipeline.bindPoint() == vk::PipelineBindPoint::eCompute ? m_computeSetBound : m_graphicsSetBound;
    if (!setBound)
    {
        m_commandBuffer.bindDescriptorSets(pipeline.bindPoint(), m_device.pipelineLayout(), 0,
            m_device.bindlessHeap().set(), {});
        setBound = true;
    }
}

auto CommandBuffer::pushConstants(std::span<const std::byte> data, std::uint32_t offset) const -> void
{
    assert(offset % 4 == 0 && data.size() % 4 == 0 && "Push constant offset and size must be multiples of 4");
    assert(offset + data.size() <= maxPushConstantSize && "Push constants exceed the shared block");

    m_commandBuffer.pushConstants<std::byte>(m_device.pipelineLayout(), vk::ShaderStageFlagBits::eAll, offset,
        vk::ArrayProxy<const std::byte>{ static_cast<std::uint32_t>(data.size()), data.data() });
}

auto CommandBuffer::dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
    std::uint32_t groupCountZ) const -> void
{
    m_commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ);
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

auto CommandBuffer::draw(std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t firstVertex,
    std::uint32_t firstInstance) const -> void
{
    m_commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

auto CommandBuffer::bindIndexBuffer(const Buffer& buffer, std::size_t offset) const -> void
{
    m_commandBuffer.bindIndexBuffer(buffer.vk(), offset, vk::IndexType::eUint32);
}

auto CommandBuffer::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex,
    std::int32_t vertexOffset, std::uint32_t firstInstance) const -> void
{
    m_commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

auto CommandBuffer::blit(ImageViewHandle src, ImageViewHandle dst, Filter filter) const -> void
{
    const auto& srcView = m_device.get(src);
    const auto& dstView = m_device.get(dst);

    const auto layers = [](const ImageView& view) {
        return vk::ImageSubresourceLayers{
            .aspectMask = deriveImageAspectFlags(view.format()),
            .mipLevel = view.range().baseMipLevel,
            .baseArrayLayer = view.range().baseArrayLayer,
            .layerCount = view.range().arrayLayerCount,
        };
    };
    const auto corner = [](const ImageView& view) {
        return vk::Offset3D{
            .x = static_cast<std::int32_t>(view.extent().x),
            .y = static_cast<std::int32_t>(view.extent().y),
            .z = static_cast<std::int32_t>(view.extent().z),
        };
    };

    const vk::ImageBlit2 region{
        .srcSubresource = layers(srcView),
        .srcOffsets = std::array{ vk::Offset3D{ 0, 0, 0 }, corner(srcView) },
        .dstSubresource = layers(dstView),
        .dstOffsets = std::array{ vk::Offset3D{ 0, 0, 0 }, corner(dstView) },
    };
    m_commandBuffer.blitImage2(vk::BlitImageInfo2{
        .srcImage = srcView.vkImage(),
        .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
        .dstImage = dstView.vkImage(),
        .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        .regionCount = 1,
        .pRegions = &region,
        .filter = toVulkan(filter),
    });
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
            .levelCount = image.mipLevels(),
            .baseArrayLayer = 0,
            .layerCount = image.arrayLayers(),
        },
    };

    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    });
}

auto CommandBuffer::makeTransferWritesVisible() const -> void
{
    vk::MemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    };

    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    });
}

auto CommandBuffer::memoryBarrier(ResourceState before, ResourceState after) const -> void
{
    const auto src = toVulkan(before);
    const auto dst = toVulkan(after);
    vk::MemoryBarrier2 barrier{
        .srcStageMask = src.stageMask,
        .srcAccessMask = src.accessMask,
        .dstStageMask = dst.stageMask,
        .dstAccessMask = dst.accessMask,
    };

    m_commandBuffer.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    });
}

auto CommandBuffer::generateMipmaps(ImageHandle imageHandle, ResourceState currentState) const
    -> void
{
    if (currentState != ResourceState::CopyDst)
        transitionImageLayout(imageHandle, currentState, ResourceState::CopyDst);

    auto& image = m_device.get(imageHandle);
    assert(m_device.supportsMipGeneration(image.format()) && "Format cannot be mip-mapped by linear blits");
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

        auto toOffset = [](Extent3D extent) {
            return vk::Offset3D{
                .x = static_cast<std::int32_t>(extent.x),
                .y = static_cast<std::int32_t>(extent.y),
                .z = static_cast<std::int32_t>(extent.z),
            };
        };

        blit.srcSubresource.mipLevel = i - 1;
        blit.srcOffsets = std::array{
            vk::Offset3D{ 0, 0, 0 },
            toOffset(detail::mipExtent(image.extent(), i - 1)),
        };
        blit.dstSubresource.mipLevel = i;
        blit.dstOffsets = std::array{
            vk::Offset3D{ 0, 0, 0 },
            toOffset(detail::mipExtent(image.extent(), i)),
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

auto CommandBuffer::resetQueries(const QueryPool& pool, std::uint32_t first, std::uint32_t count) const
    -> void
{
    m_commandBuffer.resetQueryPool(pool.vk(), first, count);
}

auto CommandBuffer::writeTimestamp(const QueryPool& pool, std::uint32_t query, TimestampStage stage) const
    -> void
{
    m_commandBuffer.writeTimestamp2(toVulkan(stage), pool.vk(), query);
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

namespace
{
auto toVulkanRegions(std::span<const BufferImageCopy> regions, vk::ImageAspectFlags aspectMask)
    -> std::vector<vk::BufferImageCopy2>
{
    std::vector<vk::BufferImageCopy2> result;
    result.reserve(regions.size());
    std::ranges::transform(regions, std::back_inserter(result), [&](const auto& region) {
        return vk::BufferImageCopy2{
            .bufferOffset = region.bufferOffset,
            // Zero means "tightly packed to imageExtent", which is the layout
            // detail::subresourceFootprints produces.
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = vk::ImageSubresourceLayers{
                .aspectMask = aspectMask,
                .mipLevel = region.mipLevel,
                .baseArrayLayer = region.baseArrayLayer,
                .layerCount = region.arrayLayerCount,
            },
            .imageOffset = vk::Offset3D{ 0, 0, 0 },
            .imageExtent = toVulkan(region.extent),
        };
    });
    return result;
}
}

auto CommandBuffer::copyBufferToImage(const Buffer& src, ImageHandle dst,
    std::span<const BufferImageCopy> regions) const -> void
{
    if (regions.empty())
        return;

    const auto& image = m_device.get(dst);
    const auto copyRegions = toVulkanRegions(regions, deriveImageAspectFlags(image.format()));

    m_commandBuffer.copyBufferToImage2(vk::CopyBufferToImageInfo2{
        .srcBuffer = src.vk(),
        .dstImage = image.vk(),
        .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        .regionCount = static_cast<std::uint32_t>(copyRegions.size()),
        .pRegions = copyRegions.data(),
    });
}

auto CommandBuffer::copyImageToBuffer(ImageHandle src, const Buffer& dst,
    std::span<const BufferImageCopy> regions) const -> void
{
    if (regions.empty())
        return;

    const auto& image = m_device.get(src);
    const auto copyRegions = toVulkanRegions(regions, deriveImageAspectFlags(image.format()));

    m_commandBuffer.copyImageToBuffer2(vk::CopyImageToBufferInfo2{
        .srcImage = image.vk(),
        .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
        .dstBuffer = dst.vk(),
        .regionCount = static_cast<std::uint32_t>(copyRegions.size()),
        .pRegions = copyRegions.data(),
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

    for (const auto& attachment : { cmd.depthAttachment, cmd.stencilAttachment })
    {
        if (!attachment)
            continue;
        const auto& image = m_device.get(attachment->imageView);
        return vk::Extent2D{ image.extent().x, image.extent().y };
    }

    assert(false && "RenderingCmd has no attachments");
    std::unreachable();
}
}
