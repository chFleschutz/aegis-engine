module;
#include <format>
#include <functional>
#include <optional>
#include <ostream>
#include <print>

module aegis.renderer;

namespace aegis::renderer
{
auto Renderer::create(const Desc& desc) -> std::expected<Renderer, Error>
{
    auto swapchain = desc.device.createSwapchain(rhi::Swapchain::Desc{
        .context = desc.context,
        .preferredExtent = rhi::Extent2D{ desc.window.extent() },
    });
    if (!swapchain)
        return std::unexpected{ Error::RHIInitializationFailed };

    auto commandPool = desc.device.createCommandPool({
        .name = "FrameCmdPool",
        .queueFamily = desc.device.graphicsQueue().family(),
    });
    if (!commandPool)
        return std::unexpected{ Error::RHIInitializationFailed };

    auto frameContext = createFrameContext(desc.device, *commandPool);
    if (!frameContext)
        return std::unexpected{ Error::RHIInitializationFailed };

    return std::expected<Renderer, Error>{
        std::in_place,
        desc.context,
        desc.device,
        std::move(*swapchain),
        std::move(*commandPool),
        std::move(*frameContext)
    };
}

Renderer::Renderer(rhi::Context& context,
    rhi::Device& device,
    rhi::Swapchain&& swapchain,
    rhi::CommandPool&& commandPool,
    std::vector<FrameContext>&& frameContext) :
    m_context{ context },
    m_device{ device },
    m_swapchain{ std::move(swapchain) },
    m_commandPool{ std::move(commandPool) },
    m_frameContext{ std::move(frameContext) }
{
}

auto Renderer::renderFrame(std::function<void(const FrameInfo&)> drawFunc) noexcept -> void
{
    auto frameInfo = beginFrame();
    if (!frameInfo)
        return;

    drawFunc(*frameInfo);

    endFrame();
}

auto Renderer::resize(rhi::Extent2D newSize)
{
    std::ignore = m_device->waitIdle();

    auto swapchain = m_device.createSwapchain(rhi::Swapchain::RecreateDesc{
        .preferredExtent = newSize,
        .oldSwapchain = m_swapchain,
    });

    if (!swapchain)
    {
        std::println("Failed to recreate swapchain");
        return;
    }
    m_swapchain = std::move(*swapchain);

    for (auto& [image, name, usage, scaleFactor] : m_resDependent)
    {
        auto width = static_cast<uint32_t>(m_swapchain.extent().x * scaleFactor);
        auto height = static_cast<uint32_t>(m_swapchain.extent().y * scaleFactor);
        auto depth = image.ref().extent.z;

        auto newImage = m_device.createImage({
            .name = name,
            .extent = rhi::Extent3D{ width, height, depth },
            .format = image.format(),
            .usage = usage,
            .mipLevels = image.ref().levelCount,
            .arrayLayers = image.ref().layerCount,
        });

        if (!newImage)
        {
            std::println("Failed to recreate resolution dependent image '{}'", name);
            continue;
        }
        image = std::move(*newImage);
    }

    m_needsResize = false;
}

auto Renderer::registerResolutionDependentResource(rhi::Image::Desc desc,
    float scaleFactor) noexcept -> std::expected<rhi::ImageRef, rhi::Error>
{
    auto image = m_device.createImage(desc);
    if (!image)
        return std::unexpected{ image.error() };

    m_resDependent.emplace_back(*image, desc.name, desc.usage, scaleFactor);
    return std::expected<rhi::ImageRef, rhi::Error>(std::move(image->ref()));
}

auto Renderer::createFrameContext(
    const rhi::Device& device,
    const rhi::CommandPool& pool) noexcept
    -> std::expected<std::vector<FrameContext>, Error>
{
    std::expected<std::vector<FrameContext>, Error> frameContext;
    frameContext.emplace();
    frameContext->reserve(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i)
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

auto Renderer::beginFrame() noexcept -> std::expected<FrameInfo, Error>
{
    auto& [cmd, imageAvailable, timePoint] = m_frameContext[m_currentFrame];
    if (!m_device.graphicsQueue().wait(timePoint))
        return std::unexpected{ Error::FrameBeginFailed };

    auto acquiredImage = m_swapchain.acquireNextImage(imageAvailable);
    if (!acquiredImage && acquiredImage.error().code == rhi::ErrorCode::OutOfDate)
    {
        m_pendingResize = true;
        return std::unexpected{ Error::SwapchainOutOfDate };
    }
    if (!acquiredImage)
        return std::unexpected{ Error::FrameBeginFailed };

    return std::expected<FrameInfo, Error>{
        std::in_place,
        cmd,
        acquiredImage->imageRef,
        m_currentFrame,
    };
}

auto Renderer::endFrame() noexcept -> void
{
    auto& [cmd, imageAvailable, timePoint] = m_frameContext[m_currentFrame];

    auto newSubmitTime = m_device.graphicsQueue().submit({
        .commandBuffer = cmd,
        .waitSemaphore = imageAvailable,
        .signalSemaphore = m_swapchain.currentPresentReady(),
    });
    if (!newSubmitTime)
    {
        std::println("Failed to submit to queue");
        return;
    }
    timePoint = *newSubmitTime;

    auto result = m_swapchain.present(m_device.presentQueue());
    if (!result)
    {
        std::println("Failed to present to queue");
        return;
    }

    m_currentFrame = (m_currentFrame + 1) % m_frameContext.size();
}
}
