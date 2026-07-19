module;
#include "GLFW/glfw3.h"

#include <algorithm>
#include <cassert>
#include <expected>
#include <limits>
#include <syncstream>

module aegis.rhi;
import :swapchain;
import :context;
import :device;
import :debug;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi::detail
{
auto chooseSwapchainExtent(Extent2D preferred, Extent2D minExtent, Extent2D maxExtent,
    Extent2D currentExtent) -> Extent2D
{
    if (currentExtent.x != std::numeric_limits<uint32_t>::max() &&
        currentExtent.y != std::numeric_limits<uint32_t>::max())
        return currentExtent;

    return Extent2D{
        std::clamp(preferred.x, minExtent.x, maxExtent.x),
        std::clamp(preferred.y, minExtent.y, maxExtent.y)
    };
}

auto chooseImageCount(std::uint32_t preferred, std::uint32_t minImageCount,
    std::uint32_t maxImageCount) -> std::uint32_t
{
    auto imageCount = std::max(preferred, minImageCount);
    if (maxImageCount > 0 && imageCount > maxImageCount)
        imageCount = maxImageCount;
    return imageCount;
}
}

namespace aegis::rhi
{
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
    m_currentImage = *index;

    return std::expected<AcquiredImage, Error>{
        std::in_place,
        m_imageViews[*index],
        m_semaphores[*index],
        *index,
    };
}

auto Swapchain::present(const Queue& queue) -> std::expected<void, Error>
{
    auto waitSemaphore = m_semaphores[m_currentImage].handle();
    auto swapchain = *m_swapchain;
    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &m_currentImage,
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

auto Swapchain::create(Device& device, const Desc& desc)
    -> std::expected<Swapchain, Error>
{
    return create(device, desc.context.surface(), desc.preferredExtent, vk::SwapchainKHR{});
}

auto Swapchain::create(Device& device, const RecreateDesc& desc)
    -> std::expected<Swapchain, Error>
{
    return create(device, desc.oldSwapchain.surface(), desc.preferredExtent, desc.oldSwapchain.handle());
}

auto Swapchain::create(Device& device, vk::SurfaceKHR surface,
    Extent2D preferredExtent, vk::SwapchainKHR oldSwapchain)
    -> std::expected<Swapchain, Error>
{
    auto surfaceCaps = querySurfaceCapabilities(device.physicalDevice(), surface);
    if (!surfaceCaps)
        return std::unexpected{ surfaceCaps.error() };

    auto extent = querySwapchainExtent(preferredExtent, *surfaceCaps);

    auto format = querySwapchainFormat(device.physicalDevice(), surface);
    if (!format)
        return std::unexpected{ format.error() };

    auto presentMode = queryPresentMode(device.physicalDevice(), surface);
    if (!presentMode)
        return std::unexpected{ presentMode.error() };

    auto swapchain = createSwapchain(*device,
        extent,
        *format,
        *presentMode,
        *surfaceCaps,
        surface,
        oldSwapchain);
    if (!swapchain)
        return std::unexpected(swapchain.error());

    auto images = createImages(device, *swapchain);
    if (!images)
        return std::unexpected(images.error());

    auto imageViews = createImageViews(device, *images, extent, toRHI(format->format));
    if (!imageViews)
        return std::unexpected{ imageViews.error() };

    auto semaphores = createSemaphores(device, images->size());
    if (!semaphores)
        return std::unexpected{ semaphores.error() };

    return Swapchain{
        std::move(*swapchain),
        std::move(*imageViews),
        std::move(*semaphores),
        surface,
        extent,
        toRHI(format->format)
    };
}

auto Swapchain::querySurfaceCapabilities(
    const vk::raii::PhysicalDevice& physicalDevice,
    vk::SurfaceKHR surface)
    -> std::expected<vk::SurfaceCapabilitiesKHR, Error>
{
    auto surfaceCaps = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    if (!surfaceCaps.has_value())
        return makeError(toRHI(surfaceCaps.result));
    return std::expected<vk::SurfaceCapabilitiesKHR, Error>{ surfaceCaps.value };
}

auto Swapchain::querySwapchainExtent(Extent2D preferred,
    const vk::SurfaceCapabilitiesKHR& caps)
    -> Extent2D
{
    return detail::chooseSwapchainExtent(preferred, toRHI(caps.minImageExtent),
        toRHI(caps.maxImageExtent), toRHI(caps.currentExtent));
}

auto Swapchain::queryPresentMode(
    const vk::raii::PhysicalDevice& physicalDevice,
    vk::SurfaceKHR surface)
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
    const vk::raii::Device& device,
    Extent2D extent,
    vk::SurfaceFormatKHR surfaceFormat,
    vk::PresentModeKHR presentMode,
    const vk::SurfaceCapabilitiesKHR& surfaceCaps,
    vk::SurfaceKHR surface,
    vk::SwapchainKHR oldSwapchain)
    -> std::expected<vk::raii::SwapchainKHR, Error>
{
    auto minImageCount = chooseSwapImageCount(surfaceCaps);

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = surface,
        .minImageCount = minImageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = toVulkan(extent),
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true,
        .oldSwapchain = oldSwapchain,
    };

    auto swapchain = device.createSwapchainKHR(createInfo);
    if (!swapchain.has_value())
        return makeError(toRHI(swapchain.result));

    return std::move(*swapchain);
}

auto Swapchain::createImages(const Device& device, const vk::raii::SwapchainKHR& swapchain)
    -> std::expected<std::vector<vk::Image>, Error>
{
    auto images = swapchain.getImages();
    if (!images.has_value())
        return makeError(toRHI(images.result));

    for (size_t i = 0; i < images->size(); ++i)
    {
        debug::setName(*device, images->at(i), std::format("SwapchainImage{}", i));
    }

    return std::move(*images);
}

auto Swapchain::createImageViews(
    Device& device,
    const std::vector<vk::Image>& images,
    Extent2D extent,
    Format format)
    -> std::expected<std::vector<ImageViewHandle>, Error>
{
    if (images.empty())
        return makeError(ErrorCode::Unknown);

    std::vector<ImageViewHandle> imageViews;
    imageViews.reserve(images.size());

    for (size_t i = 0; i < images.size(); ++i)
    {
        auto imageView = device.createImageView(images[i],
            ImageView::Desc{
                .name = std::format("SwapchainImageView{}", i),
                .extent = Extent3D{ extent },
                .format = format,
                .range = {},
            });
        if (!imageView)
            return std::unexpected{ imageView.error() };

        imageViews.emplace_back(*imageView);
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
        auto semaphore = device.createSemaphore({ std::format("SwapchainPresentReadySemaphore{}", i) });
        if (!semaphore)
            return std::unexpected{ semaphore.error() };
        semaphores.emplace_back(std::move(*semaphore));
    }

    return semaphores;
}

auto Swapchain::chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps) -> uint32_t
{
    constexpr uint32_t desiredImageCount{ 3 };
    return detail::chooseImageCount(desiredImageCount, caps.minImageCount, caps.maxImageCount);
}

Swapchain::Swapchain(
    vk::raii::SwapchainKHR swapchain,
    std::vector<ImageViewHandle> imageViews,
    std::vector<Semaphore> semaphores,
    vk::SurfaceKHR surface,
    Extent2D extent,
    Format format) :
    m_swapchain{ std::move(swapchain) },
    m_imageViews{ std::move(imageViews) },
    m_semaphores{ std::move(semaphores) },
    m_surface{ surface },
    m_extent{ extent },
    m_surfaceFormat{ format }
{
}
}
