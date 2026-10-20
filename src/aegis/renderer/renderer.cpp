module;
#include <cassert>

module aegis.renderer;

import std;

namespace aegis::renderer
{
namespace
{
// One per frame in flight, like swapchain images: a frame never draws into a target that the
// previous, possibly still running, frame draws into. TransferSrc allows reading a frame back,
// TransferDst blitting one in, as onto a swapchain image.
auto offscreenTargetDesc(std::string_view name, rhi::Extent2D extent, rhi::Format format) -> Texture::Desc
{
    return {
        .name = name,
        .extent = rhi::Extent3D{ extent },
        .format = format,
        .usage = rhi::ImageUsage::ColorAttachment | rhi::ImageUsage::Sampled |
                 rhi::ImageUsage::TransferSrc | rhi::ImageUsage::TransferDst,
    };
}
}

auto Renderer::create(const Desc& desc) -> std::expected<std::unique_ptr<Renderer>, Error>
{
    std::optional<rhi::Swapchain> swapchain;
    auto extent = desc.offscreen.extent;
    auto format = desc.offscreen.format;
    if (desc.window)
    {
        auto created = desc.device.createSwapchain(rhi::Swapchain::Desc{
            .context = desc.context,
            .preferredExtent = rhi::Extent2D{ desc.window->extent() },
        });
        if (!created)
            return makeError(ErrorCode::RHIInitializationFailed, "Failed to create the swapchain", created.error());

        extent = created->extent();
        format = created->surfaceFormat();
        swapchain.emplace(std::move(*created));
    }

    auto commandPool = desc.device.createCommandPool({
        .name = "FrameCmdPool",
        .queueFamily = desc.device.graphicsQueue().family(),
    });
    if (!commandPool)
        return makeError(ErrorCode::RHIInitializationFailed, "Failed to create the frame command pool", commandPool.error());

    auto frameContext = createFrameContext(desc.device, *commandPool);
    if (!frameContext)
        return std::unexpected{ std::move(frameContext.error()) };

    auto renderer = std::make_unique<Renderer>(
        desc.context,
        desc.device,
        std::move(swapchain),
        extent,
        format,
        std::move(*commandPool),
        std::move(*frameContext));

    if (renderer->offscreen())
    {
        if (auto result = renderer->createOffscreenTargets(); !result)
            return std::unexpected{ result.error() };
    }

    return renderer;
}

Renderer::Renderer(rhi::Context& context,
    rhi::Device& device,
    std::optional<rhi::Swapchain>&& swapchain,
    rhi::Extent2D extent,
    rhi::Format targetFormat,
    rhi::CommandPool&& commandPool,
    std::vector<FrameContext>&& frameContext) :
    m_context{ context },
    m_device{ device },
    m_swapchain{ std::move(swapchain) },
    m_extent{ extent },
    m_targetFormat{ targetFormat },
    m_commandPool{ std::move(commandPool) },
    m_frameContext{ std::move(frameContext) }
{
}

Renderer::~Renderer()
{
    m_device.waitIdle();
    for (const auto& target : m_offscreenTargets)
        freeTexture(m_device, target);
}

auto Renderer::offscreenTarget(std::uint32_t frameIndex) const -> const Texture&
{
    assert(offscreen() && "Only an offscreen renderer has offscreen targets");
    return m_offscreenTargets[frameIndex];
}

auto createTexture(rhi::Device& device, const Texture::Desc& desc) -> std::expected<Texture, rhi::Error>
{
    auto image = device.createImage(rhi::Image::Desc{
        .name = desc.name,
        .type = desc.type,
        .extent = desc.extent,
        .format = desc.format,
        .usage = desc.usage,
        .mipLevels = desc.mipLevels,
        .arrayLayers = desc.arrayLayers,
    });
    if (!image)
        return std::unexpected{ image.error() };

    // The resolved mip count: desc.mipLevels may be the fullMipChain sentinel. The usage is what makes
    // Device write the view's bindless descriptors; without it the texture is invisible to shaders.
    auto view = device.createImageView(*image,
        rhi::ImageView::Desc{
            .name = desc.name,
            .extent = desc.extent,
            .format = desc.format,
            .range = rhi::ImageView::Range{
                .baseMipLevel = 0,
                .mipLevelCount = device.get(*image).mipLevels(),
                .baseArrayLayer = 0,
                .arrayLayerCount = desc.arrayLayers,
            },
            .usage = desc.usage,
        });
    if (!view)
        return std::unexpected{ view.error() };

    return Texture{ *image, *view };
}

auto recreateTexture(rhi::Device& device, const Texture& texture, const Texture::Desc& desc)
    -> std::expected<void, rhi::Error>
{
    auto image = device.replace(texture.image,
        rhi::Image::Desc{
            .name = desc.name,
            .type = desc.type,
            .extent = desc.extent,
            .format = desc.format,
            .usage = desc.usage,
            .mipLevels = desc.mipLevels,
            .arrayLayers = desc.arrayLayers,
        });
    if (!image)
        return std::unexpected{ image.error() };

    auto view = device.replace(texture.view,
        *image,
        rhi::ImageView::Desc{
            .name = desc.name,
            .extent = desc.extent,
            .format = desc.format,
            .range = rhi::ImageView::Range{
                .mipLevelCount = device.get(*image).mipLevels(),
                .arrayLayerCount = desc.arrayLayers,
            },
            .usage = desc.usage,
        });
    if (!view)
        return std::unexpected{ view.error() };
    return {};
}

auto freeTexture(rhi::Device& device, const Texture& texture) -> void
{
    device.free(texture.view);
    device.free(texture.image);
}

auto Renderer::renderFrame(const std::function<void(const FrameInfo&)>& drawFunc) noexcept
    -> std::expected<void, Error>
{
    // Minimized: there is no surface to draw to, and resize() keeps needsResize() set until it is back.
    if (m_zeroExtent)
        return {};

    auto frameInfo = beginFrame();
    if (!frameInfo)
    {
        // An out-of-date swapchain only skips the frame: the caller resizes before the next one.
        if (frameInfo.error().code == ErrorCode::SwapchainOutOfDate)
            return {};
        return std::unexpected{ std::move(frameInfo.error()) };
    }

    const auto& cmd = frameInfo->cmd;
    cmd.begin();
    cmd.beginLabel("Frame");
    // Every frame redraws the whole target, so its previous contents are discarded.
    cmd.transitionImageLayout(frameInfo->target,
        rhi::ResourceState::Unknown,
        rhi::ResourceState::ColorAttachment);

    drawFunc(*frameInfo);

    // An offscreen target stays a color attachment: there is no present, and ePresentSrcKHR is not
    // even valid without VK_KHR_swapchain.
    if (m_swapchain)
    {
        cmd.transitionImageLayout(frameInfo->target,
            rhi::ResourceState::ColorAttachment,
            rhi::ResourceState::Present);
    }
    cmd.endLabel();
    cmd.end();

    return endFrame();
}

auto Renderer::resize(rhi::Extent2D newSize) -> std::expected<void, Error>
{
    // Note: Resources need to be replaced immediately as the swapchain size changed.
    // The full sync point is required to ensure GPU is not using any resources anymore.
    // The resize happens before the frame-begin where resources are deleted, by enqueuing the resource
    // with the current frames timeline value before the actual frame-begins; the deletion happens this frame.
    // This is the intended behavior but worth noting anyway.

    // A zero extent (minimized) cannot hold images or a swapchain: skip frames until a real size arrives.
    m_zeroExtent = newSize.x == 0 || newSize.y == 0;
    if (m_zeroExtent)
    {
        m_needsResize = true;
        return {};
    }

    std::ignore = m_device->waitIdle();

    if (m_swapchain)
    {
        auto swapchain = m_device.createSwapchain(rhi::Swapchain::RecreateDesc{
            .preferredExtent = newSize,
            .oldSwapchain = *m_swapchain,
        });

        if (!swapchain)
        {
            return makeError(ErrorCode::SwapchainRecreateFailed, "Failed to recreate the swapchain", swapchain.error());
        }
        m_swapchain = std::move(*swapchain);
        m_extent = m_swapchain->extent();
    }
    else
    {
        // The loop below recreates the offscreen targets.
        m_extent = newSize;
    }

    for (std::uint32_t i = 0; i < m_offscreenTargets.size(); ++i)
    {
        const auto name = std::format("OffscreenTarget{}", i);
        if (auto recreated = recreateTexture(m_device, m_offscreenTargets[i], offscreenTargetDesc(name, m_extent, m_targetFormat));
            !recreated)
        {
            return makeError(ErrorCode::ResourceRecreateFailed,
                std::format("Failed to recreate '{}'", name),
                recreated.error());
        }
    }

    m_needsResize = false;
    return {};
}

auto Renderer::createFrameContext(
    const rhi::Device& device,
    const rhi::CommandPool& pool) noexcept
    -> std::expected<std::vector<FrameContext>, Error>
{
    std::expected<std::vector<FrameContext>, Error> frameContext;
    frameContext.emplace();
    frameContext->reserve(maxFramesInFlight);
    for (std::uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        auto cmd = device.createCommandBuffer({
            .name = std::format("CmdBufferFrame{}", i),
            .pool = pool,
        });
        if (!cmd)
            return makeError(ErrorCode::RHIInitializationFailed, std::format("Failed to create CmdBufferFrame{}", i), cmd.error());

        auto semaphore = device.createSemaphore({
            .name = std::format("ImageAvailableSemaphoreFrame{}", i),
        });
        if (!semaphore)
            return makeError(ErrorCode::RHIInitializationFailed, std::format("Failed to create ImageAvailableSemaphoreFrame{}", i), semaphore.error());

        frameContext->emplace_back(FrameContext{ std::move(*cmd), std::move(*semaphore) });
    }
    return frameContext;
}

auto Renderer::createOffscreenTargets() -> std::expected<void, Error>
{
    for (std::uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        const auto name = std::format("OffscreenTarget{}", i);
        auto target = createTexture(m_device, offscreenTargetDesc(name, m_extent, m_targetFormat));
        if (!target)
            return makeError(ErrorCode::RHIInitializationFailed, std::format("Failed to create OffscreenTarget{}", i), target.error());
        m_offscreenTargets.emplace_back(*target);
    }
    return {};
}

auto Renderer::beginFrame() noexcept -> std::expected<FrameInfo, Error>
{
    auto& [cmd, imageAvailable, timePoint] = m_frameContext[m_currentFrame];
    if (!m_device.graphicsQueue().wait(timePoint))
        return makeError(ErrorCode::FrameBeginFailed, "Failed to wait for the frame's previous submission");

    m_device.setFrameCompleted(timePoint);

    if (!m_swapchain)
    {
        return std::expected<FrameInfo, Error>{
            std::in_place,
            cmd,
            m_offscreenTargets[m_currentFrame].view,
            m_currentFrame,
            m_extent,
        };
    }

    auto acquiredImage = m_swapchain->acquireNextImage(imageAvailable);
    if (!acquiredImage && acquiredImage.error().code == rhi::ErrorCode::OutOfDate)
    {
        m_needsResize = true;
        return makeError(ErrorCode::SwapchainOutOfDate);
    }
    if (!acquiredImage)
        return makeError(ErrorCode::FrameBeginFailed, "Failed to acquire a swapchain image", acquiredImage.error());

    return std::expected<FrameInfo, Error>{
        std::in_place,
        cmd,
        acquiredImage->image,
        m_currentFrame,
        m_extent,
    };
}

auto Renderer::endFrame() noexcept -> std::expected<void, Error>
{
    auto& [cmd, imageAvailable, timePoint] = m_frameContext[m_currentFrame];

    // Offscreen frames are paced by the timeline semaphore alone; there is nothing to acquire or
    // present, so no binary semaphores either.
    auto newSubmitTime = m_swapchain
                             ? m_device.graphicsQueue().submit({
                                   .commandBuffer = cmd,
                                   .waitSemaphore = imageAvailable,
                                   .signalSemaphore = m_swapchain->currentPresentReady(),
                               })
                             : m_device.graphicsQueue().submit(cmd);
    if (!newSubmitTime)
    {
        // The acquire already signalled imageAvailable and nothing will wait on it, so the slot's next
        // acquire would signal an already signalled semaphore (VUID-vkAcquireNextImageKHR-semaphore-01286).
        // A semaphore cannot be unsignalled: replace it once the GPU is idle.
        if (m_swapchain)
        {
            m_device.waitIdle();
            if (auto semaphore = m_device.createSemaphore({ .name = std::format("ImageAvailableSemaphoreFrame{}", m_currentFrame) }))
                imageAvailable = std::move(*semaphore);
        }
        return makeError(ErrorCode::QueueSubmitFailed, "Failed to submit to the graphics queue", newSubmitTime.error());
    }
    timePoint = *newSubmitTime;

    // The frame was submitted, so its slot is used up even if the present fails: staying on it would
    // wait on its own timeline value and let ImGui's per-frame buffers drift out of phase.
    const auto presented = m_swapchain ? m_swapchain->present(m_device.presentQueue())
                                       : std::expected<void, rhi::Error>{};
    m_currentFrame = (m_currentFrame + 1) % m_frameContext.size();
    if (!presented)
    {
        return makeError(ErrorCode::SwapchainPresentFailed, "Failed to present to the queue", presented.error());
    }
    return {};
}
}
