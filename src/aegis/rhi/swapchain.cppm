module;
#include <expected>
#include <vector>

export module aegis.rhi:swapchain;
import :common;
import :fwd;
import :error;
import :sync;
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
        Swapchain* oldSwapchain = nullptr;
    };

    struct AcquiredImage
    {
        // TODO: Don't use vulkan types
        vk::Image image;
        vk::ImageView view;
        const Semaphore& presentReady;
        std::uint32_t imageIndex;
    };

    static auto create(const Desc& desc) -> std::expected<Swapchain, Error>;

    [[nodiscard]] auto operator*() const -> vk::SwapchainKHR { return *m_swapchain; }
    [[nodiscard]] auto handle() const -> vk::SwapchainKHR { return *m_swapchain; }
    [[nodiscard]] auto surfaceFormat() const -> Format { return m_surfaceFormat; }
    [[nodiscard]] auto extent() const -> Extent2D { return m_extent; }
    [[nodiscard]] auto needsRecreation() const -> bool { return m_needsRecreation; }

    [[nodiscard]] auto acquireNextImage(
        const Semaphore& imageAvailable) -> std::expected<AcquiredImage, Error>;

    [[nodiscard]] auto present(const Queue& queue, const AcquiredImage& image) -> std::expected<void, Error>;

private:
    Swapchain(
        vk::raii::SwapchainKHR swapchain,
        std::vector<vk::Image> images,
        std::vector<vk::raii::ImageView> imageViews,
        std::vector<Semaphore> semaphores,
        vk::Extent2D extent,
        Format format);

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

    [[nodiscard]] static auto createSemaphores(const Device& device,
        std::size_t imageCount)
        -> std::expected<std::vector<Semaphore>, Error>;

    [[nodiscard]] static auto chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps)
        -> uint32_t;

    vk::raii::SwapchainKHR m_swapchain;
    std::vector<vk::Image> m_images;
    std::vector<vk::raii::ImageView> m_imageViews;
    std::vector<Semaphore> m_semaphores;
    Extent2D m_extent;
    Format m_surfaceFormat;
    std::uint32_t m_currentImage{ 0 };
    bool m_needsRecreation{ false };
};
}
