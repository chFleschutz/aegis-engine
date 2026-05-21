module;
#include "GLFW/glfw3.h"

#include <algorithm>
#include <cassert>
#include <expected>
#include <print>
#include <syncstream>

module aegis.rhi;
import :swapchain;
import :context;
import :device;
import :image_ref;
import :vulkan;

namespace aegis::rhi
{
auto Swapchain::create(const Desc& desc)
    -> std::expected<Swapchain, Error>
{
    const auto& physicalDevice = desc.device.physicalDevice();
    const auto& device = desc.device.device();
    const auto& surface = desc.context.surface();

    auto surfaceCaps = querySurfaceCapabilities(physicalDevice, surface);
    if (!surfaceCaps)
        return std::unexpected{ surfaceCaps.error() };

    auto extent = querySwapchainExtent(desc.extent, *surfaceCaps);

    auto format = querySwapchainFormat(desc.device.physicalDevice(), desc.context.surface());
    if (!format)
        return std::unexpected{ format.error() };

    auto presentMode = queryPresentMode(physicalDevice, surface);
    if (!presentMode)
        return std::unexpected{ presentMode.error() };

    auto swapchain = createSwapchain(desc, extent, *format, *presentMode, *surfaceCaps);
    if (!swapchain)
        return std::unexpected(swapchain.error());

    auto images = createImages(*swapchain);
    if (!images)
        return std::unexpected(images.error());

    auto imageViews = createImageViews(device, *images, format->format);
    if (!imageViews)
        return std::unexpected{ imageViews.error() };

    auto semaphores = createSemaphores(desc.device, images->size());
    if (!semaphores)
        return std::unexpected{ semaphores.error() };

    return std::expected<Swapchain, Error>{
        std::in_place,
        std::move(*swapchain),
        std::move(*images),
        std::move(*imageViews),
        std::move(*semaphores),
        extent,
        toRHI(format->format)
    };
}

Swapchain::Swapchain(
    vk::raii::SwapchainKHR swapchain,
    std::vector<vk::Image> images,
    std::vector<vk::raii::ImageView> imageViews,
    std::vector<Semaphore> semaphores,
    vk::Extent2D extent,
    Format format) :
    m_swapchain{ std::move(swapchain) },
    m_images{ std::move(images) },
    m_imageViews{ std::move(imageViews) },
    m_semaphores{ std::move(semaphores) },
    m_extent{ extent.width, extent.height },
    m_surfaceFormat{ format }
{
}

auto Swapchain::acquireNextImage(const Semaphore& signalSemaphore)
    -> std::expected<AcquiredImage, Error>
{
    auto index = m_swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), *signalSemaphore);
    if (index.result == vk::Result::eSuboptimalKHR)
    {
        // Swapchain needs to be recreated but ok to continue
        m_needsRecreation = true;
    }
    else if (index.result == vk::Result::eErrorOutOfDateKHR)
    {
        // Swapchain needs to be recreated and cannot continue
        m_needsRecreation = true;
        return makeError(ErrorCode::OutOfDate);
    }
    else if (index.result != vk::Result::eSuccess)
    {
        return makeError(toRHI(index.result));
    }

    return std::expected<AcquiredImage, Error>{
        std::in_place,
        ImageRef{ m_images[*index], *m_imageViews[*index] },
        m_semaphores[*index],
        *index,
    };
}

auto Swapchain::present(const Queue& queue, const AcquiredImage& image) -> std::expected<void, Error>
{
    auto waitSemaphore = *image.presentReady;
    auto swapchain = *m_swapchain;
    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image.imageIndex,
    };

    auto result = queue->presentKHR(presentInfo);
    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        m_needsRecreation = true;
    }
    else if (result != vk::Result::eSuccess)
    {
        return makeError(toRHI(result));
    }

    return {};
}

auto Swapchain::querySurfaceCapabilities(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface)
    -> std::expected<vk::SurfaceCapabilitiesKHR, Error>
{
    auto surfaceCaps = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    if (!surfaceCaps.has_value())
        return makeError(toRHI(surfaceCaps.result));
    return surfaceCaps.value;
}

auto Swapchain::querySwapchainExtent(Extent2D preferred,
    const vk::SurfaceCapabilitiesKHR& caps)
    -> vk::Extent2D
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    return vk::Extent2D{
        std::clamp(preferred.x, caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(preferred.y, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

auto Swapchain::queryPresentMode(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface)
    -> std::expected<vk::PresentModeKHR, Error>
{
    auto presentModes = physicalDevice.getSurfacePresentModesKHR(surface);
    if (!presentModes.has_value())
        return makeError(toRHI(presentModes.result));

    // Prefer mailbox
    if (std::ranges::any_of(*presentModes,
        [&](const auto& presentMode) {
            return presentMode == vk::PresentModeKHR::eMailbox;
        }))
        return vk::PresentModeKHR::eMailbox;

    // Fallback fifo
    if (std::ranges::any_of(*presentModes,
        [](const auto& presentMode) {
            return presentMode == vk::PresentModeKHR::eFifo;
        }))
        return vk::PresentModeKHR::eFifo;

    return makeError(ErrorCode::Unknown);
}

auto Swapchain::querySwapchainFormat(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::SurfaceKHR& surface)
    -> std::expected<vk::SurfaceFormatKHR, Error>
{
    auto availableFormats = physicalDevice.getSurfaceFormatsKHR(surface);
    if (!availableFormats.has_value())
        return makeError(toRHI(availableFormats.result));

    if (availableFormats->empty())
        return makeError(ErrorCode::Unknown);

    const auto it = std::ranges::find_if(*availableFormats,
        [](const auto& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });

    if (it == availableFormats->end())
        return availableFormats->front();
    return *it;
}

auto Swapchain::createSwapchain(
    const Desc& desc,
    vk::Extent2D extent,
    vk::SurfaceFormatKHR surfaceFormat,
    vk::PresentModeKHR presentMode,
    const vk::SurfaceCapabilitiesKHR& surfaceCaps)
    -> std::expected<vk::raii::SwapchainKHR, Error>
{
    auto minImageCount = chooseSwapImageCount(surfaceCaps);

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = desc.context.surface(),
        .minImageCount = minImageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true,
        .oldSwapchain = desc.oldSwapchain ? desc.oldSwapchain->handle() : nullptr,
    };

    auto swapchain = desc.device->createSwapchainKHR(createInfo);
    if (!swapchain.has_value())
        return makeError(toRHI(swapchain.result));

    return std::move(*swapchain);
}

auto Swapchain::createImages(const vk::raii::SwapchainKHR& swapchain)
    -> std::expected<std::vector<vk::Image>, Error>
{
    auto images = swapchain.getImages();
    if (!images.has_value())
        return makeError(toRHI(images.result));

    return std::move(*images);
}

auto Swapchain::createImageViews(
    const vk::raii::Device& device,
    const std::vector<vk::Image>& images,
    vk::Format imageFormat)
    -> std::expected<std::vector<vk::raii::ImageView>, Error>
{
    if (images.empty())
        return makeError(ErrorCode::Unknown);

    std::vector<vk::raii::ImageView> imageViews;
    vk::ImageViewCreateInfo createInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = imageFormat,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    for (const auto& image : images)
    {
        createInfo.image = image;
        auto imageView = device.createImageView(createInfo);
        if (!imageView.has_value())
            return makeError(toRHI(imageView.result));

        imageViews.emplace_back(std::move(*imageView));
    }

    return imageViews;
}

auto Swapchain::createSemaphores(
    const Device& device,
    std::size_t imageCount)
    -> std::expected<std::vector<Semaphore>, Error>
{
    std::vector<Semaphore> semaphores;
    semaphores.reserve(imageCount);

    for (std::size_t i = 0; i < imageCount; ++i)
    {
        auto semaphore = Semaphore::create({ device });
        if (!semaphore)
            return std::unexpected{ semaphore.error() };
        semaphores.emplace_back(std::move(*semaphore));
    }

    return semaphores;
}

auto Swapchain::chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps) -> uint32_t
{
    constexpr uint32_t desiredImageCount{ 3 };
    uint32_t imageCount = std::max(desiredImageCount, caps.minImageCount);
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    return imageCount;
}
}
