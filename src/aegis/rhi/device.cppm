module;
#include <cassert>

export module aegis.rhi:device;
import std;
import :buffer;
import :command_buffer;
import :command_pool;
import :deletion_queue;
import :error;
import :image;
import :image_view;
import :memory;
import :pipeline;
import :query;
import :queue;
import :resource_pool;
import :sampler;
import :swapchain;
import :upload_manager;
import aegis.platform;
import vulkan;

export namespace aegis::rhi::detail
{
/// @brief The kind of a physical device, in the order device selection prefers (best first).
enum class DeviceKind : std::uint8_t
{
    Discrete,
    Integrated,
    Virtual,
    Cpu,
    Other,
};

/// @brief What device selection knows about a physical device. Plain values, so a test TU can build one.
struct DeviceCandidate
{
    DeviceKind kind{ DeviceKind::Other };
    std::uint32_t apiVersion{ 0 };
    std::uint32_t maxImageDimension2D{ 0 };
    bool queuesComplete{ false };
    bool extensionsSupported{ false };
};

/// @brief The lowest Vulkan version the engine runs on (1.3.0, as vk::makeApiVersion(0, 1, 3, 0)).
inline constexpr std::uint32_t minApiVersion{ (1u << 22) | (3u << 12) };

/// @brief Scores a physical device; the highest score wins.
/// @return std::nullopt if the device cannot run the engine (API below 1.3, missing extensions or
///         queue families). The device kind dominates (discrete > integrated > virtual > CPU > other);
///         maxImageDimension2D only breaks ties within a kind.
[[nodiscard]] auto rankPhysicalDevice(const DeviceCandidate& candidate) -> std::optional<std::uint64_t>;

/// @brief Whether 'deviceName' contains 'filter', ignoring case. An empty filter matches every name.
[[nodiscard]] auto matchesDeviceName(std::string_view deviceName, std::string_view filter) -> bool;

/// @brief What one queue family can do.
struct QueueFamilyInfo
{
    bool graphics{ false };
    bool compute{ false };
    bool transfer{ false };
    bool present{ false }; // Can present to the surface; always false when headless
};

inline constexpr std::uint32_t noQueueFamily{ std::numeric_limits<std::uint32_t>::max() };

/// @brief The queue family index chosen for each role, or noQueueFamily.
struct QueueFamilySelection
{
    std::uint32_t graphics{ noQueueFamily };
    std::uint32_t compute{ noQueueFamily };
    std::uint32_t transfer{ noQueueFamily };
    std::uint32_t present{ noQueueFamily };

    /// @param presentation Whether a present family is required (false when headless).
    [[nodiscard]] auto isComplete(bool presentation) const -> bool;
};

/// @brief Assigns queue families to roles. Compute and transfer prefer a dedicated family and fall
///        back to the graphics family; present prefers the graphics family and never falls back
///        (a family that cannot present to the surface must not be used for it).
/// @param presentation Whether a surface exists. Without one, 'present' stays noQueueFamily.
[[nodiscard]] auto pickQueueFamilies(std::span<const QueueFamilyInfo> families, bool presentation)
    -> QueueFamilySelection;
}

export namespace aegis::rhi
{
class Device
{
    friend class Context;

public:
    struct Desc
    {
        /// @brief If not empty, only a device whose name contains this (case-insensitive) is used, and
        ///        creation fails if none does. Empty picks the best device (detail::rankPhysicalDevice).
        std::string deviceName;
    };

    struct Properties
    {
        vk::PhysicalDeviceProperties2 core;
        vk::PhysicalDeviceVulkan11Properties vk11;
        vk::PhysicalDeviceVulkan12Properties vk12;
        vk::PhysicalDeviceVulkan13Properties vk13;
    };

    struct Capabilities
    {
        bool meshShaders{ false };
    };

    /// @brief Required only with a surface: a headless device never presents.
    static constexpr std::array presentationExtensions{
        vk::KHRSwapchainExtensionName,
    };

    Device(
        vk::raii::PhysicalDevice pd,
        vk::raii::Device device,
        Allocator allocator,
        Queue graphicsQueue,
        Queue computeQueue,
        Queue transferQueue,
        std::optional<Queue> presentQueue,
        Capabilities capabilities,
        Properties properties,
        BindlessHeap bindlessHeap,
        vk::raii::PipelineLayout pipelineLayout);

    /// @brief Waits for the GPU first: destroying resources it may still use is invalid.
    ~Device();

    [[nodiscard]] auto operator->() const noexcept -> const vk::raii::Device* { return &m_device; }
    [[nodiscard]] auto operator*() const noexcept -> const vk::raii::Device& { return m_device; }

    [[nodiscard]] auto physicalDevice() const noexcept -> const vk::raii::PhysicalDevice&;
    [[nodiscard]] auto device() const noexcept -> const vk::raii::Device& { return m_device; }
    [[nodiscard]] auto allocator() const noexcept -> const Allocator& { return m_allocator; }
    [[nodiscard]] auto graphicsQueue() noexcept -> Queue& { return m_graphicsQueue; }
    [[nodiscard]] auto presentQueue() noexcept -> Queue&
    {
        assert(m_presentQueue && "A headless device has no present queue");
        return *m_presentQueue;
    }

    /// @brief Created without a surface: no present queue, and no swapchain can be created.
    [[nodiscard]] auto headless() const noexcept -> bool { return !m_presentQueue; }
    [[nodiscard]] auto properties() const noexcept -> const Properties& { return m_properties; }
    [[nodiscard]] auto capabilities() const noexcept -> const Capabilities& { return m_capabilities; }
    [[nodiscard]] auto bindlessHeap() const noexcept -> const BindlessHeap& { return m_bindlessHeap; }

    /// @brief The layout every pipeline uses: set 0 is the bindless heap, plus
    ///        CommandBuffer::maxPushConstantSize bytes of push constants visible to all stages.
    [[nodiscard]] auto pipelineLayout() const noexcept -> vk::PipelineLayout { return *m_pipelineLayout; }

    /// @note A returned reference stays valid until the resource is freed or replaced; creating other
    ///       resources never moves it.
    [[nodiscard]] auto get(BufferHandle handle) const -> const Buffer& { return m_buffers.get(handle); }
    [[nodiscard]] auto get(ImageHandle handle) const -> const Image& { return m_images.get(handle); }

    [[nodiscard]] auto get(ImageViewHandle handle) const -> const ImageView&
    {
        return m_imageViews.get(handle);
    }

    [[nodiscard]] auto get(SamplerHandle handle) const -> const Sampler& { return m_samplers.get(handle); }

    [[nodiscard]] auto createBuffer(const Buffer::Desc& desc) -> std::expected<BufferHandle, Error>;
    [[nodiscard]] auto createImage(const Image::Desc& desc) -> std::expected<ImageHandle, Error>;
    [[nodiscard]] auto createImageView(ImageHandle image, const ImageView::Desc& desc)
        -> std::expected<ImageViewHandle, Error>;
    [[nodiscard]] auto createImageView(vk::Image imageSrc, const ImageView::Desc& desc)
        -> std::expected<ImageViewHandle, Error>;
    [[nodiscard]] auto createSampler(const Sampler::Desc& desc) -> std::expected<SamplerHandle, Error>;

    auto replace(BufferHandle handle, const Buffer::Desc& desc)
        -> std::expected<BufferHandle, Error>;
    auto replace(ImageHandle handle, const Image::Desc& desc)
        -> std::expected<ImageHandle, Error>;
    /// @warning Rewrites the view's bindless descriptors in place, so no pending GPU work may still
    ///          read them; wait for the device to idle first (as Renderer::resize does).
    auto replace(ImageViewHandle view, ImageHandle image, const ImageView::Desc& desc)
        -> std::expected<ImageViewHandle, Error>;

    auto free(BufferHandle handle) -> void;
    auto free(ImageHandle handle) -> void;
    auto free(ImageViewHandle handle) -> void;
    auto free(SamplerHandle handle) -> void;

    /// @brief How many buffers, images, image views and samplers exist. A freed resource stops counting at
    ///        once, though its memory is released only when the GPU is done with it. For leak checks.
    struct LiveResources
    {
        std::size_t buffers{ 0 };
        std::size_t images{ 0 };
        std::size_t imageViews{ 0 };
        std::size_t samplers{ 0 };

        auto operator==(const LiveResources&) const -> bool = default;
    };
    [[nodiscard]] auto liveResources() const noexcept -> LiveResources
    {
        return { m_buffers.liveCount(), m_images.liveCount(), m_imageViews.liveCount(), m_samplers.liveCount() };
    }

    /// @brief Reports that the graphics queue reached 'frame', releasing everything freed before it.
    auto setFrameCompleted(TimelineValue frame) -> void;

    /// @brief Whether mips of 'format' can be generated on the GPU, which uses linear blits: the
    ///        format must support blit source/destination and linear filtering, and not be depth.
    [[nodiscard]] auto supportsMipGeneration(Format format) const -> bool;

    /// @brief Stages 'data' and records a copy into the buffer 'dst' at 'dstOffset'. Never blocks.
    /// @return ErrorCode::NotReady if the staging ring cannot fit it right now: flushUploads() (which
    ///         reclaims completed batches) and retry. Any other error is not retryable.
    [[nodiscard]] auto upload(BufferHandle dst, std::span<const std::byte> data,
        std::size_t alignment = 4, std::size_t dstOffset = 0) -> std::expected<void, Error>;

    /// @brief Stages 'data' and records a copy into every mip of the image 'dst'. Never blocks.
    /// @note With 'finalState' the batch leaves 'dst' in it; without, in ResourceState::CopyDst
    ///       (CopySrc when mips were generated) for the caller to transition. See
    ///       UploadManager::upload for the data layout the caller must match.
    [[nodiscard]] auto upload(ImageHandle dst, std::span<const std::byte> data, bool generateMips = false,
        std::optional<ResourceState> finalState = std::nullopt) -> std::expected<void, Error>;

    /// @brief Submits the open upload batch on the graphics queue and reclaims completed staging.
    /// @return The timeline value the submitted batch signals, or nullopt if nothing was submitted.
    ///         The queue's submit error if the submission failed; the batch is dropped then.
    [[nodiscard]] auto flushUploads() -> std::expected<std::optional<TimelineValue>, Error>;

    [[nodiscard]] auto createCommandBuffer(const CommandBuffer::Desc& desc) const
        -> std::expected<CommandBuffer, Error>;

    [[nodiscard]] auto createCommandPool(const CommandPool::Desc& desc) const
        -> std::expected<CommandPool, Error>;

    [[nodiscard]] auto createFence(const Fence::Desc& desc) const
        -> std::expected<Fence, Error>;

    [[nodiscard]] auto createPipeline(const Pipeline::GraphicsDesc& desc) const
        -> std::expected<Pipeline, Error>;

    /// @brief Creates a timestamp query pool for work on the graphics queue.
    [[nodiscard]] auto createQueryPool(const QueryPool::Desc& desc) const
        -> std::expected<QueryPool, Error>;

    [[nodiscard]] auto createPipeline(const Pipeline::ComputeDesc& desc) const
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] auto createSemaphore(const Semaphore::Desc& desc) const
        -> std::expected<Semaphore, Error>;

    [[nodiscard]] auto createSwapchain(const Swapchain::Desc& desc)
        -> std::expected<Swapchain, Error>;

    [[nodiscard]] auto createSwapchain(const Swapchain::RecreateDesc& desc)
        -> std::expected<Swapchain, Error>;

    auto waitIdle() const noexcept -> void;

private:
    using FeatureChain = vk::StructureChain<
        vk::DeviceCreateInfo,
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceMeshShaderFeaturesEXT>;

    using QueueFamilyIndices = detail::QueueFamilySelection;

    [[nodiscard]] static auto create(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface,
        const Desc& desc)
        -> std::expected<std::unique_ptr<Device>, Error>;

    [[nodiscard]] static auto createPhysicalDevice(const vk::raii::Instance& instance,
        const vk::raii::SurfaceKHR& surface, std::string_view deviceName)
        -> std::expected<vk::raii::PhysicalDevice, Error>;

    [[nodiscard]] static auto queryQueueFamilies(const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::SurfaceKHR& surface)
        -> QueueFamilyIndices;

    [[nodiscard]] static auto createQueue(const vk::raii::Device& device, std::string_view name,
        std::uint32_t queueFamily)
        -> std::expected<Queue, Error>;

    [[nodiscard]] static auto createDevice(const vk::raii::PhysicalDevice& pd,
        const Capabilities& capabilities, const QueueFamilyIndices& queueFamilyIndices, bool presentation)
        -> std::expected<vk::raii::Device, Error>;

    [[nodiscard]] static auto queryCapabilities(const vk::raii::PhysicalDevice& pd) -> Capabilities;
    [[nodiscard]] static auto requiredExtensions(bool presentation) -> std::vector<const char*>;
    [[nodiscard]] static auto queryExtensions(const Capabilities& caps, bool presentation)
        -> std::vector<const char*>;
    [[nodiscard]] static auto supportsExtensions(const vk::raii::PhysicalDevice& pd, bool presentation)
        -> bool;
    [[nodiscard]] static auto createFeatureChain(const Capabilities& capabilities) -> FeatureChain;
    [[nodiscard]] static auto queryProperties(const vk::raii::PhysicalDevice& pd) -> Properties;
    [[nodiscard]] static auto createPipelineLayout(const vk::raii::Device& device,
        const BindlessHeap& bindlessHeap)
        -> std::expected<vk::raii::PipelineLayout, Error>;

    /// @brief The timeline value at which something freed now is safe to release. Already-submitted
    ///        work may reference it, and so may the frame being recorded, which is submitted next.
    [[nodiscard]] auto retireValue() const noexcept -> TimelineValue;

    vk::raii::PhysicalDevice m_physicalDevice;
    vk::raii::Device m_device;
    Allocator m_allocator;
    Queue m_graphicsQueue;
    Queue m_computeQueue;
    Queue m_transferQueue;
    std::optional<Queue> m_presentQueue; // nullopt when headless
    Properties m_properties;
    Capabilities m_capabilities;
    ResourcePool<Buffer> m_buffers;
    ResourcePool<Image> m_images;
    ResourcePool<ImageView> m_imageViews;
    ResourcePool<Sampler> m_samplers;
    DeletionQueue m_deletionQueue;
    BindlessHeap m_bindlessHeap;
    vk::raii::PipelineLayout m_pipelineLayout;
    std::optional<UploadManager> m_uploadManager;
};
}
