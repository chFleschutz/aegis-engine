module;
#include <cstdint>
#include <expected>
#include <vector>

export module aegis.rhi:swapchain;
import :common;
import :fwd;
import :error;
import :image_view;
import :sync;
import vulkan_hpp;

export namespace aegis::rhi::detail
{
/// @brief Picks a suitable swapchain extent.
/// @param preferred The preferred extent that is chosen if it is suitable.
/// @param minExtent, maxExtent Minimum and maximum for the new extent.
/// @param currentExtent Non uint32_t::max values indicate to keep the current
///                      extent, otherwise the preferred extent is considered.
/// @note Free function taking plain values rather than vk::SurfaceCapabilitiesKHR: a test TU
///       cannot name a vk:: type, so the Vulkan unpacking stays in swapchain.cpp.
[[nodiscard]] auto chooseSwapchainExtent(Extent2D preferred, Extent2D minExtent, Extent2D maxExtent,
    Extent2D currentExtent) -> Extent2D;

/// @brief Clamps a preferred swapchain image count into the range the surface supports.
/// @param maxImageCount Zero means the surface imposes no upper bound.
[[nodiscard]] auto chooseImageCount(std::uint32_t preferred, std::uint32_t minImageCount,
    std::uint32_t maxImageCount) -> std::uint32_t;
}

export namespace aegis::rhi
{
class Swapchain
{
    friend class Device;

public:
    struct Desc
    {
        const Context& context;
        Extent2D preferredExtent;
    };

    struct RecreateDesc
    {
        Extent2D preferredExtent;
        const Swapchain& oldSwapchain;
    };

    struct AcquiredImage
    {
        ImageViewHandle image;
        const Semaphore& presentReady;
        std::uint32_t imageIndex;
    };

    Swapchain(const Swapchain&) = delete;
    Swapchain(Swapchain&&) noexcept = default;
    ~Swapchain() = default;

    auto operator=(const Swapchain&) -> Swapchain& = delete;
    auto operator=(Swapchain&&) noexcept -> Swapchain& = default;

    [[nodiscard]] auto operator*() const -> vk::SwapchainKHR { return *m_swapchain; }

    [[nodiscard]] auto handle() const -> vk::SwapchainKHR { return *m_swapchain; }
    [[nodiscard]] auto surface() const -> vk::SurfaceKHR { return m_surface; }
    [[nodiscard]] auto surfaceFormat() const -> Format { return m_surfaceFormat; }
    [[nodiscard]] auto extent() const -> Extent2D { return m_extent; }
    [[nodiscard]] auto currentImageIndex() const -> std::uint32_t { return m_currentImage; }
    [[nodiscard]] auto imageViews() const -> const std::vector<ImageViewHandle>& { return m_imageViews; }

    [[nodiscard]] auto currentPresentReady() const -> const Semaphore&
    {
        return m_semaphores[m_currentImage];
    }

    [[nodiscard]] auto needsRecreation() const -> bool { return m_needsRecreation; }

    [[nodiscard]] auto acquireNextImage(const Semaphore& signalSemaphore)
        -> std::expected<AcquiredImage, Error>;

    [[nodiscard]] auto present(const Queue& queue) -> std::expected<void, Error>;

private:
    [[nodiscard]] static auto create(Device& device, const Desc& desc)
        -> std::expected<Swapchain, Error>;

    [[nodiscard]] static auto create(Device& device, const RecreateDesc& desc)
        -> std::expected<Swapchain, Error>;

    [[nodiscard]] static auto create(Device& device, vk::SurfaceKHR surface,
        Extent2D preferredExtent, vk::SwapchainKHR oldSwapchain)
        -> std::expected<Swapchain, Error>;

    [[nodiscard]] static auto querySurfaceCapabilities(
        const vk::raii::PhysicalDevice& physicalDevice,
        vk::SurfaceKHR surface)
        -> std::expected<vk::SurfaceCapabilitiesKHR, Error>;

    [[nodiscard]] static auto querySwapchainExtent(
        Extent2D preferred,
        const vk::SurfaceCapabilitiesKHR& caps)
        -> Extent2D;

    [[nodiscard]] static auto queryPresentMode(
        const vk::raii::PhysicalDevice& physicalDevice,
        vk::SurfaceKHR surface)
        -> std::expected<vk::PresentModeKHR, Error>;

    [[nodiscard]] static auto querySwapchainFormat(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::SurfaceKHR& surface)
        -> std::expected<vk::SurfaceFormatKHR, Error>;

    [[nodiscard]] static auto createSwapchain(
        const vk::raii::Device& device,
        Extent2D extent,
        vk::SurfaceFormatKHR surfaceFormat,
        vk::PresentModeKHR presentMode,
        const vk::SurfaceCapabilitiesKHR& surfaceCaps,
        vk::SurfaceKHR surface,
        vk::SwapchainKHR oldSwapchain)
        -> std::expected<vk::raii::SwapchainKHR, Error>;

    [[nodiscard]] static auto createImages(const Device& device, const vk::raii::SwapchainKHR& swapchain)
        -> std::expected<std::vector<vk::Image>, Error>;

    [[nodiscard]] static auto createImageViews(
        Device& device,
        const std::vector<vk::Image>& images,
        Extent2D extent,
        Format format)
        -> std::expected<std::vector<ImageViewHandle>, Error>;

    [[nodiscard]] static auto createSemaphores(
        const Device& device,
        std::size_t imageCount)
        -> std::expected<std::vector<Semaphore>, Error>;

    [[nodiscard]] static auto chooseSwapImageCount(const vk::SurfaceCapabilitiesKHR& caps)
        -> uint32_t;

    Swapchain(
        vk::raii::SwapchainKHR swapchain,
        std::vector<ImageViewHandle> imageViews,
        std::vector<Semaphore> semaphores,
        vk::SurfaceKHR surface,
        Extent2D extent,
        Format format);

    vk::raii::SwapchainKHR m_swapchain;
    std::vector<ImageViewHandle> m_imageViews;
    std::vector<Semaphore> m_semaphores;
    vk::SurfaceKHR m_surface;
    Extent2D m_extent;
    Format m_surfaceFormat;
    std::uint32_t m_currentImage{ 0 };
    bool m_needsRecreation{ false };
};
}
