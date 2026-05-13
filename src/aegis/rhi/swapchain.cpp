module;
#include "GLFW/glfw3.h"

#include <algorithm>
#include <cassert>
#include <expected>
#include <print>

module aegis.rhi;
import :swapchain;

namespace aegis::rhi
{
auto Swapchain::create(const Desc& desc) -> std::expected<Swapchain, Error>
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

    return Swapchain{ std::move(*swapchain),
                      std::move(*images),
                      std::move(*imageViews),
                      extent,
                      fromVk(format->format) };
}

Swapchain::Swapchain(
    vk::raii::SwapchainKHR swapchain,
    std::vector<vk::Image> images,
    std::vector<vk::raii::ImageView> imageViews,
    vk::Extent2D extent,
    Format format) :
    m_swapchain{ std::move(swapchain) },
    m_images{ std::move(images) },
    m_imageViews{ std::move(imageViews) },
    m_extent{ extent },
    m_surfaceFormat{ format }
{
}

auto Swapchain::querySurfaceCapabilities(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface) -> std::expected<vk::SurfaceCapabilitiesKHR, Error>
{
    auto [result, surfaceCaps] = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to get surface capabilities");
    return surfaceCaps;
}

auto Swapchain::querySwapchainExtent(vk::Extent2D preferred, const vk::SurfaceCapabilitiesKHR& caps)
    -> vk::Extent2D
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    return vk::Extent2D{
        std::clamp(preferred.width, caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(preferred.height, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

auto Swapchain::queryPresentMode(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::SurfaceKHR& surface) -> std::expected<vk::PresentModeKHR, Error>
{
    auto [result, presentModes] = physicalDevice.getSurfacePresentModesKHR(surface);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to get surface present modes");

    // Prefer mailbox
    if (std::ranges::any_of(presentModes, [&](const auto& presentMode) {
            return presentMode == vk::PresentModeKHR::eMailbox;
        }))
        return vk::PresentModeKHR::eMailbox;

    // Fallback fifo
    if (std::ranges::any_of(presentModes, [](const auto& presentMode) {
            return presentMode == vk::PresentModeKHR::eFifo;
        }))
        return vk::PresentModeKHR::eFifo;

    return vkError(vk::Result::eErrorUnknown, "Failed to find suitable present mode");
}

auto Swapchain::querySwapchainFormat(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::SurfaceKHR& surface) -> std::expected<vk::SurfaceFormatKHR, Error>
{
    auto [result, availableFormats] = physicalDevice.getSurfaceFormatsKHR(surface);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to get surface formats");

    if (availableFormats.empty())
        return vkError(vk::Result::eErrorUnknown, "No valid surface format found");

    const auto it = std::ranges::find_if(availableFormats, [](const auto& format) {
        return format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });

    if (it == availableFormats.end())
        return availableFormats.front();
    return *it;
}

auto Swapchain::createSwapchain(
    const Desc& desc,
    vk::Extent2D extent,
    vk::SurfaceFormatKHR surfaceFormat,
    vk::PresentModeKHR presentMode,
    const vk::SurfaceCapabilitiesKHR& surfaceCaps) -> std::expected<vk::raii::SwapchainKHR, Error>
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
    };

    auto [result, swapchain] = desc.device.device().createSwapchainKHR(createInfo);
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to create swapchain");

    return std::move(swapchain);
}

auto Swapchain::createImages(const vk::raii::SwapchainKHR& swapchain)
    -> std::expected<std::vector<vk::Image>, Error>
{
    auto [result, images] = swapchain.getImages();
    if (result != vk::Result::eSuccess)
        return vkError(result, "Failed to get swapchain images");

    return std::move(images);
}

auto Swapchain::createImageViews(
    const vk::raii::Device& device,
    const std::vector<vk::Image>& images,
    vk::Format imageFormat) -> std::expected<std::vector<vk::raii::ImageView>, Error>
{
    if (images.empty())
        return vkError(vk::Result::eErrorUnknown, "Swapchain contains no images");

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
        auto [result, imageView] = device.createImageView(createInfo);
        if (result != vk::Result::eSuccess)
            return vkError(result, "Failed to create swapchain image views");

        imageViews.emplace_back(std::move(imageView));
    }

    return imageViews;
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
