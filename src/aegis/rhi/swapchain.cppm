module;
#include <vector>

export module aegis.rhi:swapchain;
import :context;
import :device;

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

    explicit Swapchain(const Desc& desc);

private:
    auto createSwapchain(const Desc& desc) -> void;
    auto createImages() -> void;
    auto createImageViews(const Desc& desc) -> void;

    [[nodiscard]] static auto chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps)
        -> uint32_t;
    [[nodiscard]] static auto chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& fmts)
        -> vk::SurfaceFormatKHR;
    [[nodiscard]] static auto choosePresentMode(const std::vector<vk::PresentModeKHR>& presentModes)
        -> vk::PresentModeKHR;
    [[nodiscard]] auto chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& caps) const
        -> vk::Extent2D;

    vk::raii::SwapchainKHR m_swapchain{ nullptr };
    std::vector<vk::Image> m_images;
    std::vector<vk::raii::ImageView> m_imageViews;
    vk::Extent2D m_extent;
    vk::SurfaceFormatKHR m_surfaceFormat;
};
}
