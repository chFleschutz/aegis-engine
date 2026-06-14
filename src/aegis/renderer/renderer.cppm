module;
#include <expected>
#include <functional>
#include <vector>
#include <string_view>

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

    static constexpr std::uint32_t maxFramesInFlight{ 2 };

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<Renderer, Error>;

    Renderer(rhi::Context& context,
        rhi::Device& device,
        rhi::Swapchain&& swapchain,
        rhi::CommandPool&& commandPool,
        std::vector<FrameContext>&& frameContext);

    [[nodiscard]] auto swapchain() const noexcept -> const rhi::Swapchain& { return m_swapchain; }

    auto requestResize(rhi::Extent2D newSize) -> void;

    auto renderFrame(std::function<void(const FrameInfo&)> drawFunc) noexcept -> void;

private:

    [[nodiscard]] static auto createFrameContext(
        const rhi::Device& device,
        const rhi::CommandPool& pool) noexcept
        -> std::expected<std::vector<FrameContext>, Error>;

    auto beginFrame() noexcept -> std::expected<FrameInfo, Error>;
    auto endFrame() noexcept -> void;
    auto resize() -> void;

    rhi::Context& m_context;
    rhi::Device& m_device;
    rhi::Swapchain m_swapchain;
    rhi::CommandPool m_commandPool;
    std::vector<FrameContext> m_frameContext;
    std::uint32_t m_currentFrame{ 0 };
    rhi::Extent2D m_pendingSize{ 0, 0 };
    bool m_pendingResize{ false };
};
}
