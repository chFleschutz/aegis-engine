module;
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <ostream>
#include <print>

module aegis.renderer;

namespace aegis::renderer
{
auto Renderer::create(const Desc& desc) -> std::expected<std::unique_ptr<Renderer>, Error>
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

    return std::make_unique<Renderer>(
        desc.context,
        desc.device,
        std::move(*swapchain),
        std::move(*commandPool),
        std::move(*frameContext));
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

auto Renderer::createTexture(const Texture::Desc& desc) const -> std::expected<Texture, rhi::Error>
{
    auto image = m_device.createImage(rhi::Image::Desc{
        .name = desc.name,
        .extent = desc.extent,
        .format = desc.format,
        .usage = desc.usage,
        .mipLevels = desc.mipLevels,
        .arrayLayers = desc.arrayLayers,
    });
    if (!image)
        return std::unexpected{ image.error() };

    auto view = m_device.createImageView(*image,
        rhi::ImageView::Desc{
            .name = desc.name,
            .extent = desc.extent,
            .format = desc.format,
            .range = rhi::ImageView::Range{
                .baseMipLevel = 0,
                .mipLevelCount = desc.mipLevels,
                .baseArrayLayer = 0,
                .arrayLayerCount = desc.arrayLayers,
            }
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
            m_resDependent.emplace_back(desc.name, texture, desc.usage, 1.0);
            return texture;
        });
}

auto Renderer::renderFrame(const std::function<void(const FrameInfo&)>& drawFunc) noexcept -> void
{
    auto frameInfo = beginFrame();
    if (!frameInfo)
        return;

    drawFunc(*frameInfo);

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

    for (auto& [name, texture, usage, scaleFactor] : m_resDependent)
    {
        auto& image = m_device.get(texture.image);

        auto newImage = m_device.replace(texture.image,
            rhi::Image::Desc{
                .name = name,
                .extent = rhi::Extent3D{
                    static_cast<uint32_t>(m_swapchain.extent().x * scaleFactor),
                    static_cast<uint32_t>(m_swapchain.extent().y * scaleFactor),
                    image.extent().z
                },
                .format = image.format(),
                .usage = usage,
                .mipLevels = image.mipLevels(),
                .arrayLayers = image.arrayLayers(),
            });

        if (!newImage)
            std::println("Failed to recreate resolution dependent image '{}'", name);

        auto viewResult = m_device.replace(texture.view,
            *newImage,
            rhi::ImageView::Desc{
                .name = name,
                .extent = rhi::Extent3D{
                    static_cast<uint32_t>(m_swapchain.extent().x * scaleFactor),
                    static_cast<uint32_t>(m_swapchain.extent().y * scaleFactor),
                    image.extent().z
                },
                .format = image.format(),
            }
        );

        if (!viewResult)
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

    m_device.setFrameCompleted(timePoint);

    auto acquiredImage = m_swapchain.acquireNextImage(imageAvailable);
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
        m_swapchain.extent(),
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
