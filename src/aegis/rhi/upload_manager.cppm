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
import :utility;

export namespace aegis::rhi
{
/// @brief A persistently-mapped, host-visible ring buffer used to stage CPU data for GPU uploads.
/// @note The buffer itself is owned by the Device (created via Device::createBuffer); this type
///       holds the handle and frees it on destruction. Sub-allocations are handed out in FIFO
///       order and must be reclaimed (tail advanced) once the GPU no longer reads them.
class StagingBuffer
{
public:
    enum class AllocError
    {
        ZeroSizeAllocation,
        ExceedsBufferSize,
        MemoryExhausted,
    };

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
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_size; }

    /// @brief Reserves 'size' bytes aligned to 'alignment' from the ring. Never blocks.
    /// @return The allocation, or MemoryExhausted if the ring cannot satisfy it right now.
    auto allocate(std::size_t size, std::size_t alignment)
        -> std::expected<Allocation, AllocError>
    {
        if (size == 0)
            return std::unexpected{ AllocError::ZeroSizeAllocation };

        if (size > m_size)
            return std::unexpected{ AllocError::ExceedsBufferSize };

        auto alignedHead = utility::alignTo(m_head, alignment);
        if (alignedHead + size <= m_size)
        {
            // Fits before capacity -> no wrap-around
            if (!isRegionFree(alignedHead, alignedHead + size))
                return std::unexpected{ AllocError::MemoryExhausted };

            m_head = alignedHead + size;
            return Allocation{ .data = m_data, .offset = alignedHead, .size = size };
        }

        // Does not fit before capacity -> wrap-around to the front
        if (!isRegionFree(0, size))
            return std::unexpected{ AllocError::MemoryExhausted };

        m_head = size;
        return Allocation{ .data = m_data, .offset = 0, .size = size };
    }

    /// @brief Frees everything up to (and including) the given allocation end offset.
    auto reclaim(std::size_t allocEndOffset) -> void { m_tail = allocEndOffset; }

private:
    StagingBuffer(Device& device, BufferHandle handle, std::byte* data, std::size_t size);

    /// @brief Whether [start, end) (end exclusive) lies entirely inside the free region.
    [[nodiscard]] auto isRegionFree(std::size_t start, std::size_t end) const -> bool
    {
        if (start > end)
            return false; // Cannot allocate a wrapped region

        if (m_tail <= m_head)
        {
            // Occupied: [tail, head)  ->  Free: [0, tail) + [head, capacity)
            return start >= m_head || end <= m_tail;
        }

        // Occupied (wrapped): [0, head) + [tail, capacity)  ->  Free: [head, tail)
        return start >= m_head && end <= m_tail;
    }

    Device* m_device;
    BufferHandle m_handle;
    std::byte* m_data;
    std::size_t m_size;
    std::uint64_t m_head{ 0 };
    std::uint64_t m_tail{ 0 };
};

/// @brief Uploads CPU data into GPU resources without stalling the caller.
///
/// Data is copied into a staging ring buffer and a copy command is recorded into one of a small
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
    };

    // Number of command buffers cycled through; matches the renderer's frames-in-flight so an
    // upload batch can be recorded while a previous one is still executing on the GPU.
    static constexpr std::uint32_t framesInFlight{ 2 };

    [[nodiscard]] static auto create(Device& device, std::uint32_t queueFamily)
        -> std::expected<UploadManager, Error>;
    [[nodiscard]] static auto create(Device& device, std::uint32_t queueFamily, const Desc& desc)
        -> std::expected<UploadManager, Error>;

    /// @brief Stages 'data' and records a copy into 'dst' at 'dstOffset'. Never blocks.
    /// @return true if the upload was queued, false if the staging ring could not satisfy the
    ///         allocation right now. On failure the caller should flushPending() (which reclaims
    ///         completed batches) and retry.
    [[nodiscard]] auto upload(const Buffer& dst, std::span<const std::byte> data,
        std::size_t alignment = 4, std::size_t dstOffset = 0) -> bool
    {
        auto allocation = m_stagingBuffer.allocate(data.size(), alignment);
        if (!allocation)
            return false;

        std::memcpy(allocation->data + allocation->offset, data.data(), data.size());

        auto& cmd = m_cmds[m_recordIndex];
        if (!m_batchOpen)
        {
            // Safe to (re)record: a previous flush guaranteed this command buffer is not in flight.
            cmd.begin(true);
            m_batchOpen = true;
        }

        cmd.copyBuffer(m_stagingBuffer.buffer(), dst, data.size(), allocation->offset, dstOffset);
        m_pendingEndOffset = allocation->offset + allocation->size;
        return true;
    }

    /// @brief Image uploads are not implemented yet (out of scope for the buffer-first pass).
    [[nodiscard]] auto upload(const Image& dst, std::span<const std::byte> data) -> bool
    {
        // TODO: transition layout to transfer-dst optimal, record a copy per mip/layer, transition
        //       back (and optionally generate mipmaps), mirroring the buffer batch lifecycle.
        (void)dst;
        (void)data;
        return false;
    }

    /// @brief Submits the currently open batch and reclaims completed staging memory.
    /// @return The timeline value the submitted batch signals, or nullopt if nothing was submitted.
    auto flushPending(Queue& queue) -> std::optional<TimelineValue>
    {
        if (!m_batchOpen)
        {
            // Nothing to submit. Drain the oldest in-flight batch so a caller that is flush
            // ing to
            // free staging space (e.g. after upload() returned false) makes forward progress.
            if (!m_inFlight.empty())
                reclaimFront(queue);
            return std::nullopt;
        }

        auto& cmd = m_cmds[m_recordIndex];
        cmd.end();

        auto timelineValue = queue.submit(cmd);
        if (!timelineValue)
        {
            m_batchOpen = false;
            return std::nullopt;
        }

        m_inFlight.push_back(InFlight{
            .completeTime = *timelineValue,
            .endOffset = m_pendingEndOffset,
        });
        m_batchOpen = false;
        m_recordIndex = (m_recordIndex + 1) % framesInFlight;

        // Keep at most framesInFlight-1 batches outstanding so the command buffer the next batch
        // records into is free to reset. This is the only blocking point on the happy path, and it
        // only waits when the GPU is a full ring of command buffers behind.
        while (m_inFlight.size() >= framesInFlight)
            reclaimFront(queue);

        return *timelineValue;
    }

private:
    struct InFlight
    {
        TimelineValue completeTime;
        std::size_t endOffset;
    };

    UploadManager(CommandPool cmdPool, std::vector<CommandBuffer> cmds, StagingBuffer stagingBuffer);

    auto reclaimFront(Queue& queue) -> void
    {
        const auto& front = m_inFlight.front();
        queue.wait(front.completeTime);
        m_stagingBuffer.reclaim(front.endOffset);
        m_inFlight.pop_front();
    }

    CommandPool m_cmdPool;
    std::vector<CommandBuffer> m_cmds;
    StagingBuffer m_stagingBuffer;
    std::deque<InFlight> m_inFlight;
    std::uint32_t m_recordIndex{ 0 };
    bool m_batchOpen{ false };
    std::size_t m_pendingEndOffset{ 0 };
};
}
