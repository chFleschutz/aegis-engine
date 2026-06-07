module;
#include <expected>
#include <optional>

export module aegis.rhi;
import :buffer;
export import :command_buffer;
import :command_pool;
export import :commands;
export import :common;
import :context;
import :device;
import :error;
import :image;
import :image_ref;
import :image_view;
import :pipeline;
import :queue;
import :swapchain;
import :sync;

struct RHIError
{
    enum class Code
    {
        Unknown,
        InitializationFailed,
    };

    Code code;
};

export namespace aegis::rhi
{
class RHI
{
    struct RHIConstructorToken
    {
    };

public:
    struct Desc
    {
        platform::Window& window;
        std::string_view appName;
    };

    struct FrameInfo
    {
        CommandBuffer& commandBuffer;
        ImageRef& swapchainImage;
        std::uint32_t frameIndex;
    };

    [[nodiscard]] static auto create(const Desc& desc) noexcept -> std::expected<RHI, RHIError>;

    RHI(RHIConstructorToken,
        Context&& context,
        Device&& device,
        Swapchain&& swapchain,
        CommandPool&& commandPool);

    auto beginFrame() -> std::optional<FrameInfo>;
    auto submit(CommandBuffer& cmd) -> void;
    auto endFrame() -> void;

private:
    struct FrameContext
    {
        CommandBuffer commandBuffer;
        Semaphore imageAvailable;
        std::uint64_t timelineValue{ 0 };
    };

    Context m_context;
    Device m_device;
    Swapchain m_swapchain;
    CommandPool m_commandPool;

    std::vector<FrameContext> m_frameContext;
    std::uint32_t m_currentFrame{ 0 };
};
}
