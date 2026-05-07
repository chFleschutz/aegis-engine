module;
#include "GLFW/glfw3.h"


#include <algorithm>
#include <cassert>
#include <print>

module aegis.rhi;
import :swapchain;

namespace aegis::rhi
{
Swapchain::Swapchain(const Desc& desc) : m_extent{ desc.extent }
{
    createSwapchain(desc);
    createImages();
    createImageViews(desc);
}

auto Swapchain::createSwapchain(const Desc& desc) -> void
{
    const auto& physicalDevice = desc.device.physicalDevice();
    const auto& surface = desc.context.surface();

    auto [capsResult, surfaceCaps] = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    if (capsResult != vk::Result::eSuccess)
    {
        // TODO: Error
        return;
    }

    auto [formatResult, availableFormats] = physicalDevice.getSurfaceFormatsKHR(surface);
    if (formatResult != vk::Result::eSuccess)
    {
        // TODO: Error
        return;
    }

    auto [presentResult, presentModes] = physicalDevice.getSurfacePresentModesKHR(surface);
    if (presentResult != vk::Result::eSuccess)
    {
        // TODO: Error
        return;
    }

    auto minImageCount = chooseSwapImageCount(surfaceCaps);
    auto swapExtent = chooseSwapExtent(surfaceCaps);
    auto presentMode = choosePresentMode(presentModes);
    m_surfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = desc.context.surface(),
        .minImageCount = minImageCount,
        .imageFormat = m_surfaceFormat.format,
        .imageColorSpace = m_surfaceFormat.colorSpace,
        .imageExtent = swapExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true,
    };

    auto [result, swapchain] = desc.device.device().createSwapchainKHR(createInfo);
    if (result != vk::Result::eSuccess)
    {
        // TODO: Error
        return;
    }
    m_swapchain = std::move(swapchain);
    std::println("Created swapchain");
}

auto Swapchain::createImages() -> void
{
    auto [result, images] = m_swapchain.getImages();
    if (result != vk::Result::eSuccess)
    {
        // TODO: Error
        return;
    }

    m_images = std::move(images);
    std::println("Acquired swapchain images");
}

auto Swapchain::createImageViews(const Desc& desc) -> void
{
    if (!m_imageViews.empty())
    {
        // TODO: Error
        return;
    }

    vk::ImageViewCreateInfo createInfo{
        .viewType =  vk::ImageViewType::e2D,
        .format = m_surfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    for (const auto& image : m_images)
    {
        createInfo.image = image;
        auto [result, imageView] = desc.device.device().createImageView(createInfo);
        if (result != vk::Result::eSuccess)
        {
            // TODO: Error
            return;
        }
        m_imageViews.emplace_back(std::move(imageView));
    }
}

auto Swapchain::chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps) -> uint32_t
{
    constexpr uint32_t desiredImageCount{ 3 };
    uint32_t imageCount = std::max(desiredImageCount, caps.minImageCount);
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    return imageCount;
}

auto Swapchain::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& fmts)
    -> vk::SurfaceFormatKHR
{
    // TODO: Error
    assert(!fmts.empty() && "No valid surface format provided");

    const auto it = std::ranges::find_if(fmts, [](const auto& format) {
        return format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });

    if (it == fmts.end())
        return fmts.front();
    return *it;
}

auto Swapchain::choosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes)
    -> vk::PresentModeKHR
{
    // TODO: Error
    // Ensure FIFO present mode fallback is present
    assert(std::ranges::any_of(presentModes, [](const auto& presentMode) {
        return presentMode == vk::PresentModeKHR::eFifo;
    }));

    auto mailboxFound = std::ranges::any_of(presentModes, [&](const auto& presentMode) {
        return presentMode == vk::PresentModeKHR::eMailbox;
    });

    if (mailboxFound)
        return vk::PresentModeKHR::eMailbox;
    return vk::PresentModeKHR::eFifo;
}

auto Swapchain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps) const -> vk::Extent2D
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    return vk::Extent2D{
        std::clamp(m_extent.width, caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(m_extent.height, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}
}
