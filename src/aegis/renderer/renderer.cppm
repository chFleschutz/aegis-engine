module;
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

export module aegis.renderer;
import aegis.rhi;
import aegis.platform.window;

export namespace aegis::renderer
{
enum class Error
{
    RHIInitializationFailed,
    SwapchainOutOfDate,
    FrameBeginFailed,
    QueueSubmitFailed,
    SwapchainPresentFailed,
};

class Renderer
{
public:
    struct Desc
    {
        platform::Window& window;
        rhi::Context& context;
        rhi::Device& device;
    };

    struct FrameContext
    {
        rhi::CommandBuffer commandBuffer;
        rhi::Semaphore imageAvailable;
        std::uint64_t timelineValue{ 0 };
    };

    struct FrameInfo
    {
        rhi::CommandBuffer& cmd;
        rhi::ImageRef swapchainImage;
        std::uint32_t frameIndex;
    };

    struct ResolutionDependentResource
    {
        rhi::ImageHandle image;
        std::string_view name;
        rhi::ImageUsage usage;
        float scaleFactor;
    };

    static constexpr std::uint32_t maxFramesInFlight{ 2 };

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<std::unique_ptr<Renderer>, Error>;

    Renderer(rhi::Context& context,
        rhi::Device& device,
        rhi::Swapchain&& swapchain,
        rhi::CommandPool&& commandPool,
        std::vector<FrameContext>&& frameContext);

    [[nodiscard]] auto swapchain() const noexcept -> const rhi::Swapchain& { return m_swapchain; }
    [[nodiscard]] auto needsResize() const noexcept -> bool { return m_needsResize; }

    [[nodiscard]] auto resolutionDependentResources() const -> const std::vector<ResolutionDependentResource>&
    {
        return m_resDependent;
    }

    auto renderFrame(const std::function<void(const FrameInfo&)>& drawFunc) noexcept -> void;

    auto resize(rhi::Extent2D newSize) -> void;

    auto registerResolutionDependentResource(rhi::Image::Desc desc,
        float scaleFactor = 1.0f) noexcept -> std::expected<rhi::ImageHandle, rhi::Error>;

private:
    [[nodiscard]] static auto createFrameContext(
        const rhi::Device& device,
        const rhi::CommandPool& pool) noexcept
        -> std::expected<std::vector<FrameContext>, Error>;

    auto beginFrame() noexcept -> std::expected<FrameInfo, Error>;
    auto endFrame() noexcept -> void;

    rhi::Context& m_context;
    rhi::Device& m_device;
    rhi::Swapchain m_swapchain;
    rhi::CommandPool m_commandPool;
    std::vector<FrameContext> m_frameContext;
    std::vector<ResolutionDependentResource> m_resDependent;
    std::uint32_t m_currentFrame{ 0 };
    bool m_needsResize{ false };
};
}
