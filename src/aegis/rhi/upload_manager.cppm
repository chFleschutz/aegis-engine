module;
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <vector>

export module aegis.rhi:upload_manager;
import :buffer;
import :command_buffer;
import :command_pool;
import :common;
import :fwd;
import :queue;
import :resource_handle;
import :ring_allocator;
import :utility;

export namespace aegis::rhi
{
/// @brief A persistently mapped, host-visible ring buffer used to stage CPU data for GPU uploads.
/// @note The Device owns the buffer itself, this type only holds a handle and automatically frees it.
///       Sub-allocations are handed out in FIFO order and must be reclaimed after the GPU no longer needs them.
class StagingBuffer
{
public:
    using AllocError = RingAllocator::AllocError;

    struct Allocation
    {
        std::byte* data;
        std::size_t offset;
        std::size_t size;
    };

    StagingBuffer(const StagingBuffer&) = delete;
    StagingBuffer(StagingBuffer&& other) noexcept;
    ~StagingBuffer();

    auto operator=(const StagingBuffer&) -> StagingBuffer& = delete;
    auto operator=(StagingBuffer&& other) noexcept -> StagingBuffer&;

    [[nodiscard]] static auto create(Device& device, std::size_t size)
        -> std::expected<StagingBuffer, Error>;

    [[nodiscard]] auto buffer() const -> const Buffer&;
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_ring.size(); }

    /// @brief Reserves 'size' bytes aligned to 'alignment' from the ring. Never blocks.
    /// @return The allocation, or an Error if the ring cannot allocate it.
    auto allocate(std::size_t size, std::size_t alignment)
        -> std::expected<Allocation, AllocError>;

    /// @brief Frees everything up to (and including) the given allocation end offset.
    auto reclaim(std::size_t allocEndOffset) -> void { m_ring.reclaim(allocEndOffset); }

private:
    StagingBuffer(Device& device, BufferHandle handle, std::byte* data, std::size_t size);

    Device* m_device;
    BufferHandle m_handle;
    std::byte* m_data;
    RingAllocator m_ring;
};

/// @brief Uploads CPU data into GPU resources without stalling the caller.
/// Data is copied into a staging ring buffer, and a copy command is recorded into one of a small
/// ring of command buffers. `upload` never blocks: it stages the data and records the copy for the
/// current, still-open batch. `flushPending` submits the open batch on a timeline-synchronized
/// queue and reclaims completed batches, waiting on the GPU only when a full ring of command
/// buffers is already outstanding (or when explicitly draining to free space).
class UploadManager
{
public:
    struct Desc
    {
        std::uint64_t stagingBufferSize{ 64 * 1024 * 1024 };      // Default 64 MB
        std::uint64_t overflowThreshold{ stagingBufferSize / 2 }; // Threshold for the overflow path
        // Depth of the command-buffer ring: how many submitted upload batches may be outstanding on
        // the GPU before flushPending() stalls to reclaim one. Independent of any frame cadence -- a
        // caller that flushes once per frame can pass its frames-in-flight count here.
        std::uint32_t maxOutstandingBatches{ 2 };
    };

    [[nodiscard]] static auto create(Device& device, std::uint32_t queueFamily, const Desc& desc)
        -> std::expected<UploadManager, Error>;

    /// @brief Stages 'data' and records a copy into 'dst' at 'dstOffset'. Never blocks.
    /// @return true if the upload was queued, false if the staging ring could not satisfy the
    ///         allocation right now. On failure the caller should flushPending() (which reclaims
    ///         completed batches) and retry.
    [[nodiscard]] auto upload(const Buffer& dst, std::span<const std::byte> data,
        std::size_t alignment = 4, std::size_t dstOffset = 0) -> bool;

    /// @brief Stages 'data' and records a buffer->image copy for every mip of 'dst'. Never blocks.
    ///
    /// 'data' must be laid out exactly as detail::subresourceFootprints describes: mip-major, array
    /// layers packed inside each mip, tightly packed. With 'generateMips' the caller supplies mip 0
    /// only and the rest are blitted on the GPU.
    ///
    /// @note Leaves 'dst' in ResourceState::CopyDst -- or CopySrc when 'generateMips' is set, since
    ///       mip generation ends by reading the chain. **The caller performs the transition to its
    ///       read state.** UploadManager cannot know which of ShaderReadVertex / ShaderReadFragment /
    ///       ShaderReadCompute is wanted, and guessing would bake a wrong barrier into every texture.
    ///       That transition is also what establishes the real execution dependency: generateMipmaps
    ///       ends with dstStageMask = eNone.
    /// @return true if the upload was queued, false if the staging ring could not satisfy it right
    ///         now (same flush-and-retry contract as the buffer overload).
    [[nodiscard]] auto upload(ImageHandle dst, std::span<const std::byte> data,
        bool generateMips = false) -> bool;

    /// @brief Submits the currently open batch and reclaims completed staging memory.
    /// @return The timeline value the submitted batch signals, or nullopt if nothing was submitted.
    auto flushPending(Queue& queue) -> std::optional<TimelineValue>;

private:
    struct InFlight
    {
        TimelineValue completeTime{ 0 };
        std::size_t endOffset{ 0 };
    };

    UploadManager(Device& device, CommandPool cmdPool, std::vector<CommandBuffer> cmds,
        StagingBuffer stagingBuffer);

    auto reclaimFront(const Queue& queue) -> void;

    Device* m_device;
    CommandPool m_cmdPool;
    std::vector<CommandBuffer> m_cmds;
    StagingBuffer m_stagingBuffer;
    std::deque<InFlight> m_inFlight;
    std::uint32_t m_recordIndex{ 0 };
    bool m_batchOpen{ false };
    std::size_t m_pendingEndOffset{ 0 };
};
}
