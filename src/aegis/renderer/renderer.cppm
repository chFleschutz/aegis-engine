module;
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module aegis.renderer;
export import :texture;
import aegis.rhi;
import aegis.platform;

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
    /// @brief Render targets the renderer owns, used instead of a swapchain when there is no window.
    struct Offscreen
    {
        rhi::Extent2D extent{ 0, 0 };
        rhi::Format format{ rhi::Format::RGBA8_UNORM };
    };

    struct Desc
    {
        rhi::Context& context;
        rhi::Device& device;
        /// @brief nullptr renders offscreen: no swapchain, so no acquire and no present.
        const platform::Window* window{ nullptr };
        Offscreen offscreen{}; // Used only without a window
    };

    struct FrameContext
    {
        rhi::CommandBuffer commandBuffer;
        rhi::Semaphore imageAvailable;
        std::uint64_t timelineValue{ 0 };
    };

    /// @brief What a frame callback records into. 'cmd' is already begun and 'target' is in
    ///        ResourceState::ColorAttachment; the callback must leave it there. The renderer records
    ///        the transitions around the callback and ends the command buffer.
    struct FrameInfo
    {
        rhi::CommandBuffer& cmd;
        rhi::ImageViewHandle target;
        std::uint32_t frameIndex;
        rhi::Extent2D extent;
    };

    struct ResolutionDependentResource
    {
        std::string name; // Owned: the caller's Desc::name need not outlive the call
        Texture texture;
        Texture::Desc desc; // As requested (mipLevels may be fullMipChain); desc.name is cleared
        double scaleFactor;
    };

    static constexpr std::uint32_t maxFramesInFlight{ 2 };

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<std::unique_ptr<Renderer>, Error>;

    Renderer(rhi::Context& context,
        rhi::Device& device,
        std::optional<rhi::Swapchain>&& swapchain,
        rhi::Extent2D extent,
        rhi::Format targetFormat,
        rhi::CommandPool&& commandPool,
        std::vector<FrameContext>&& frameContext);

    /// @brief Waits for the GPU first: the frame command buffers and semaphores may still be in use.
    ~Renderer();

    [[nodiscard]] auto offscreen() const noexcept -> bool { return !m_swapchain; }
    [[nodiscard]] auto extent() const noexcept -> rhi::Extent2D { return m_extent; }
    [[nodiscard]] auto targetFormat() const noexcept -> rhi::Format { return m_targetFormat; }

    /// @brief The offscreen target of frame slot 'frameIndex'. Offscreen only.
    [[nodiscard]] auto offscreenTarget(std::uint32_t frameIndex) const -> const Texture&;

    [[nodiscard]] auto needsResize() const noexcept -> bool
    {
        return m_needsResize || (m_swapchain && m_swapchain->needsRecreation());
    }

    [[nodiscard]] auto createTexture(const Texture::Desc& desc) const -> std::expected<Texture, rhi::Error>;
    [[nodiscard]] auto createResolutionDependentTexture(const Texture::Desc& desc)
        -> std::expected<Texture, rhi::Error>;

    auto renderFrame(const std::function<void(const FrameInfo&)>& drawFunc) noexcept -> void;

    /// @brief Recreates the swapchain (offscreen: the targets) and every resolution-dependent texture.
    auto resize(rhi::Extent2D newSize) -> void;

private:
    [[nodiscard]] static auto createFrameContext(
        const rhi::Device& device,
        const rhi::CommandPool& pool) noexcept
        -> std::expected<std::vector<FrameContext>, Error>;

    auto createOffscreenTargets() -> std::expected<void, Error>;

    auto beginFrame() noexcept -> std::expected<FrameInfo, Error>;
    auto endFrame() noexcept -> void;

    rhi::Context& m_context;
    rhi::Device& m_device;
    std::optional<rhi::Swapchain> m_swapchain; // nullopt when offscreen
    std::vector<Texture> m_offscreenTargets;   // One per frame in flight; empty with a swapchain
    rhi::Extent2D m_extent;
    rhi::Format m_targetFormat;
    rhi::CommandPool m_commandPool;
    std::vector<FrameContext> m_frameContext;
    std::vector<ResolutionDependentResource> m_resDependent;
    std::uint32_t m_currentFrame{ 0 };
    bool m_needsResize{ false };
};
}
