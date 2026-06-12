module;
#include <expected>
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

    static constexpr std::uint32_t maxFramesInFlight{ 2 };

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<Renderer, Error>;

    Renderer(rhi::Device& device, rhi::Swapchain&& swapchain,
        rhi::CommandPool && commandPool, std::vector<FrameContext>&& frameContext);

private:
    [[nodiscard]] static auto createFrameContext(
        const rhi::Device& device,
        const rhi::CommandPool& pool) noexcept
        -> std::expected<std::vector<FrameContext>, Error>;

    rhi::Device& m_device;
    rhi::Swapchain m_swapchain;
    rhi::CommandPool m_commandPool;
    std::vector<FrameContext> m_frameContext;
    std::uint32_t m_currentFrame{ 0 };
};
}
