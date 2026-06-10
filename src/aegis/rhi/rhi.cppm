module;
#include <expected>
#include <optional>

export module aegis.rhi;
export import :buffer;
export import :command_buffer;
import :command_pool;
export import :commands;
export import :common;
import :context;
import :device;
import :error;
export import :image;
export import :image_ref;
export import :image_view;
export import :pipeline;
import :queue;
export import :swapchain;
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

    struct FrameContext
    {
        CommandBuffer commandBuffer;
        Semaphore imageAvailable;
        std::uint64_t timelineValue{ 0 };
    };

    struct FrameInfo
    {
        CommandBuffer& commandBuffer;
        ImageRef& swapchainImage;
        std::uint32_t frameIndex;
    };

    static constexpr std::uint32_t maxFramesInFlight{ 2 };

    [[nodiscard]] static auto create(const Desc& desc) noexcept -> std::expected<RHI, RHIError>;

    RHI(RHIConstructorToken,
        Context&& context,
        Device&& device,
        Swapchain&& swapchain,
        CommandPool&& commandPool,
        std::vector<FrameContext>&& frameContext);

    [[nodiscard]] auto swapchain() const noexcept -> const Swapchain& { return m_swapchain; }

    auto beginFrame() -> std::optional<FrameInfo>;
    auto submit(CommandBuffer& cmd) -> void;
    auto endFrame() -> void;

private:
    [[nodiscard]] static auto createFrameContext(
        const Device& device,
        const CommandPool& pool) noexcept
        -> std::expected<std::vector<FrameContext>, Error>;

    Context m_context;
    Device m_device;
    Swapchain m_swapchain;
    CommandPool m_commandPool;

    std::vector<FrameContext> m_frameContext;
    std::uint32_t m_currentFrame{ 0 };
};
}
