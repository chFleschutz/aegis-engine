module;
#include <expected>
#include <vector>

export module aegis.rhi:swapchain;
import :context;
import :device;
import :error;

import vulkan_hpp;

export namespace aegis::rhi
{
class Swapchain
{
public:
    struct Desc
    {
        const Context& context;
        const Device& device;
        vk::Extent2D extent;
    };

    static auto create(const Desc& desc) -> std::expected<Swapchain, Error>;

private:
    Swapchain(
        vk::raii::SwapchainKHR swapchain,
        std::vector<vk::Image> images,
        std::vector<vk::raii::ImageView> imageViews,
        vk::Extent2D extent,
        vk::SurfaceFormatKHR format);

    [[nodiscard]] static auto querySurfaceCapabilities(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface)
        -> std::expected<vk::SurfaceCapabilitiesKHR, Error>;
    [[nodiscard]] static auto querySwapchainExtent(
        vk::Extent2D preferred,
        const vk::SurfaceCapabilitiesKHR& caps)
        -> vk::Extent2D;
    [[nodiscard]] static auto queryPresentMode(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface)
        -> std::expected<vk::PresentModeKHR, Error>;
    [[nodiscard]] static auto querySwapchainFormat(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::SurfaceKHR& surface)
        -> std::expected<vk::SurfaceFormatKHR, Error>;
    [[nodiscard]] static auto createSwapchain(
        const Desc& desc,
        vk::Extent2D extent,
        vk::SurfaceFormatKHR surfaceFormat,
        vk::PresentModeKHR presentMode,
        const vk::SurfaceCapabilitiesKHR& surfaceCaps)
        -> std::expected<vk::raii::SwapchainKHR, Error>;
    [[nodiscard]] static auto createImages(const vk::raii::SwapchainKHR& swapchain)
        -> std::expected<std::vector<vk::Image>, Error>;
    [[nodiscard]] static auto createImageViews(
        const vk::raii::Device& device,
        const std::vector<vk::Image>& images,
        vk::Format imageFormat)
        -> std::expected<std::vector<vk::raii::ImageView>, Error>;

    [[nodiscard]] static auto chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps)
        -> uint32_t;

    vk::raii::SwapchainKHR m_swapchain;
    std::vector<vk::Image> m_images;
    std::vector<vk::raii::ImageView> m_imageViews;
    vk::Extent2D m_extent;
    vk::SurfaceFormatKHR m_surfaceFormat;
};
}
