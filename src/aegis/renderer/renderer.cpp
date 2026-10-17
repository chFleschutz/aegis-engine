module;
#include <cassert>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <print>
#include <string>
#include <utility>
#include <vector>

module aegis.renderer;

namespace aegis::renderer
{
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
            return std::unexpected{ Error::RHIInitializationFailed };

        extent = created->extent();
        format = created->surfaceFormat();
        swapchain.emplace(std::move(*created));
    }

    auto commandPool = desc.device.createCommandPool({
        .name = "FrameCmdPool",
        .queueFamily = desc.device.graphicsQueue().family(),
    });
    if (!commandPool)
        return std::unexpected{ Error::RHIInitializationFailed };

    auto frameContext = createFrameContext(desc.device, *commandPool);
    if (!frameContext)
        return std::unexpected{ Error::RHIInitializationFailed };

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
}

auto Renderer::offscreenTarget(std::uint32_t frameIndex) const -> const Texture&
{
    assert(offscreen() && "Only an offscreen renderer has offscreen targets");
    return m_offscreenTargets[frameIndex];
}

auto Renderer::createTexture(const Texture::Desc& desc) const -> std::expected<Texture, rhi::Error>
{
    auto image = m_device.createImage(rhi::Image::Desc{
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
    auto view = m_device.createImageView(*image,
        rhi::ImageView::Desc{
            .name = desc.name,
            .extent = desc.extent,
            .format = desc.format,
            .range = rhi::ImageView::Range{
                .baseMipLevel = 0,
                .mipLevelCount = m_device.get(*image).mipLevels(),
                .baseArrayLayer = 0,
                .arrayLayerCount = desc.arrayLayers,
            },
            .usage = desc.usage,
        });
    if (!view)
        return std::unexpected{ view.error() };

    return Texture{ *image, *view };
}

auto Renderer::createResolutionDependentTexture(
    const Texture::Desc& desc) -> std::expected<Texture, rhi::Error>
{
    return createTexture(desc)
        .transform([&](auto texture) {
            auto stored = desc;
            stored.name = {};
            m_resDependent.emplace_back(std::string{ desc.name }, texture, stored, 1.0);
            return texture;
        });
}

auto Renderer::renderFrame(const std::function<void(const FrameInfo&)>& drawFunc) noexcept -> void
{
    auto frameInfo = beginFrame();
    if (!frameInfo)
        return;

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

    endFrame();
}

auto Renderer::resize(rhi::Extent2D newSize) -> void
{
    // Note: Resources need to be replaced immediately as the swapchain size changed.
    // The full sync point is required to ensure GPU is not using any resources anymore.
    // The resize happens before the frame-begin where resources are deleted, by enqueuing the resource
    // with the current frames timeline value before the actual frame-begins; the deletion happens this frame.
    // This is the intended behavior but worth noting anyway.

    std::ignore = m_device->waitIdle();

    if (m_swapchain)
    {
        auto swapchain = m_device.createSwapchain(rhi::Swapchain::RecreateDesc{
            .preferredExtent = newSize,
            .oldSwapchain = *m_swapchain,
        });

        if (!swapchain)
        {
            std::println("Failed to recreate swapchain");
            return;
        }
        m_swapchain = std::move(*swapchain);
        m_extent = m_swapchain->extent();
    }
    else
    {
        // The offscreen targets are resolution-dependent textures, so the loop below resizes them.
        m_extent = newSize;
    }

    for (auto& [name, texture, desc, scaleFactor] : m_resDependent)
    {
        const auto extent = rhi::Extent3D{
            static_cast<std::uint32_t>(m_extent.x * scaleFactor),
            static_cast<std::uint32_t>(m_extent.y * scaleFactor),
            desc.extent.z,
        };

        auto newImage = m_device.replace(texture.image,
            rhi::Image::Desc{
                .name = name,
                .type = desc.type,
                .extent = extent,
                .format = desc.format,
                .usage = desc.usage,
                .mipLevels = desc.mipLevels,
                .arrayLayers = desc.arrayLayers,
            });
        if (!newImage)
        {
            std::println("Failed to recreate resolution dependent image '{}'", name);
            continue;
        }

        auto newView = m_device.replace(texture.view,
            *newImage,
            rhi::ImageView::Desc{
                .name = name,
                .extent = extent,
                .format = desc.format,
                .range = rhi::ImageView::Range{
                    .mipLevelCount = m_device.get(*newImage).mipLevels(),
                    .arrayLayerCount = desc.arrayLayers,
                },
                .usage = desc.usage,
            });
        if (!newView)
            std::println("Failed to recreate resolution dependent view '{}'", name);
    }

    m_needsResize = false;
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
            .pool = pool
        });
        if (!cmd)
            return std::unexpected{ Error::RHIInitializationFailed };

        auto semaphore = device.createSemaphore({
            .name = std::format("ImageAvailableSemaphoreFrame{}", i)
        });
        if (!semaphore)
            return std::unexpected{ Error::RHIInitializationFailed };

        frameContext->emplace_back(FrameContext{ std::move(*cmd), std::move(*semaphore) });
    }
    return frameContext;
}

auto Renderer::createOffscreenTargets() -> std::expected<void, Error>
{
    using namespace rhi::utility;

    // One per frame in flight, like swapchain images: a frame never draws into a target that the
    // previous, possibly still running, frame draws into. TransferSrc allows reading a frame back,
    // TransferDst blitting one in, as onto a swapchain image.
    for (std::uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        auto target = createResolutionDependentTexture({
            .name = std::format("OffscreenTarget{}", i),
            .extent = rhi::Extent3D{ m_extent },
            .format = m_targetFormat,
            .usage = rhi::ImageUsage::ColorAttachment | rhi::ImageUsage::Sampled |
                     rhi::ImageUsage::TransferSrc | rhi::ImageUsage::TransferDst,
        });
        if (!target)
            return std::unexpected{ Error::RHIInitializationFailed };
        m_offscreenTargets.emplace_back(*target);
    }
    return {};
}

auto Renderer::beginFrame() noexcept -> std::expected<FrameInfo, Error>
{
    auto& [cmd, imageAvailable, timePoint] = m_frameContext[m_currentFrame];
    if (!m_device.graphicsQueue().wait(timePoint))
        return std::unexpected{ Error::FrameBeginFailed };

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
        return std::unexpected{ Error::SwapchainOutOfDate };
    }
    if (!acquiredImage)
        return std::unexpected{ Error::FrameBeginFailed };

    return std::expected<FrameInfo, Error>{
        std::in_place,
        cmd,
        acquiredImage->image,
        m_currentFrame,
        m_extent,
    };
}

auto Renderer::endFrame() noexcept -> void
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
        std::println("Failed to submit to queue");
        return;
    }
    timePoint = *newSubmitTime;

    if (m_swapchain)
    {
        auto result = m_swapchain->present(m_device.presentQueue());
        if (!result)
        {
            std::println("Failed to present to queue");
            return;
        }
    }

    m_currentFrame = (m_currentFrame + 1) % m_frameContext.size();
}
}
