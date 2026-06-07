module;
#include <expected>
#include <optional>
#include <print>

module aegis.rhi;

namespace aegis::rhi
{
auto RHI::create(const Desc& desc) noexcept -> std::expected<RHI, RHIError>
{
    auto context = Context::create({
        .appName = desc.appName,
        .window = desc.window,
    });
    if (!context)
        return std::unexpected{ RHIError{ RHIError::Code::InitializationFailed } };

    auto device = context->createDevice({});
    if (!device)
        return std::unexpected{ RHIError{ RHIError::Code::InitializationFailed } };

    auto swapchain = device->createSwapchain({
        .context = *context,
        .extent = Extent2D{ desc.window.extent() },
    });
    if (!swapchain)
        return std::unexpected{ RHIError{ RHIError::Code::InitializationFailed } };

    auto commandPool = device->createCommandPool({
        .name = "FrameCmdPool",
        .queueFamily = device->graphicsQueue().family(),
    });
    if (!commandPool)
        return std::unexpected{ RHIError{ RHIError::Code::InitializationFailed } };

    return std::expected<RHI, RHIError>{
        std::in_place,
        RHIConstructorToken{},
        std::move(*context),
        std::move(*device),
        std::move(*swapchain),
        std::move(*commandPool)
    };
}

RHI::RHI(RHIConstructorToken,
    Context&& context,
    Device&& device,
    Swapchain&& swapchain,
    CommandPool&& commandPool) :
    m_context(std::move(context)),
    m_device(std::move(device)),
    m_swapchain(std::move(swapchain)),
    m_commandPool(std::move(commandPool))
{
}

auto RHI::beginFrame() -> std::optional<FrameInfo>
{
    auto& [cmd, imageAvailable, timePoint] = m_frameContext[m_currentFrame];
    if (!m_device.graphicsQueue().wait(timePoint))
    {
        std::println("Failed to wait for frame sync fence");
        return std::nullopt;
    }

    auto acquiredImage = m_swapchain.acquireNextImage(imageAvailable);
    if (!acquiredImage && acquiredImage.error().code == ErrorCode::OutOfDate)
        return std::nullopt;

    if (!acquiredImage)
    {
        std::println("Failed to acquire next swapchain image");
        return std::nullopt;
    }

    return std::optional{
        FrameInfo{
            .commandBuffer = cmd,
            .swapchainImage = acquiredImage->imageRef,
            .frameIndex = m_currentFrame,
        }
    };
}

auto RHI::submit(CommandBuffer& cmd) -> void
{
    // TODO: add submitting of additional cmd buffers
}

auto RHI::endFrame() -> void
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
