module;
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "../../../external/glm/glm/vector_relational.hpp"
#include "../../../external/glm/glm/gtx/scalar_relational.inl"

export module aegis.rhi:upload_manager;
import :buffer;
import :command_buffer;
import :common;
import :utility;
import :queue;
import vulkan_hpp;

export namespace aegis::rhi
{
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

    [[nodiscard]] auto buffer() const noexcept -> const Buffer& { return m_buffer; }

    auto allocate(std::size_t size, std::size_t alignment)
        -> std::expected<Allocation, AllocError>
    {
        if (size == 0)
            return std::unexpected{ AllocError::ZeroSizeAllocation };

        if (size > m_buffer.size())
            return std::unexpected{ AllocError::ExceedsBufferSize };

        auto alignedHead = utility::alignTo(m_head, alignment);
        if (alignedHead + size <= m_buffer.size())
        {
            // Fits before capacity -> no wrap-around
            if (!isRegionFree(alignedHead, alignedHead + size))
                return std::unexpected{ AllocError::MemoryExhausted };

            m_head = alignedHead + size;
            return Allocation{
                .data = m_buffer.data(),
                .offset = alignedHead,
                .size = size,
            };
        }

        // Does not fit before capacity -> wrap-around
        if (!isRegionFree(0, size))
            return std::unexpected{ AllocError::MemoryExhausted };

        m_head = size;
        return Allocation{
            .data = m_buffer.data(),
            .offset = 0,
            .size = size,
        };
    }

    auto reclaim(std::size_t allocEndOffset)
    {
        // TODO: maybe check if offset is in allocated space
        m_tail = allocEndOffset;
    }

private:
    [[nodiscard]] auto isRegionFree(size_t start, std::size_t end) const -> bool
    {
        if (start > end)
            return false; // Cannot allocate wrapped region

        if (m_tail <= m_head)
        {
            // Occupied: [tail, head) (non-wrapped)
            // Free: [0, tail) + [head, capacity)
            return start >= m_head || end < m_tail;
        }

        // Occupied: [0, head) + (tail, capacity) (wrapped)
        // Free: [head, tail)
        return start >= m_head && end < m_tail;
    }

    Buffer m_buffer;
    std::uint64_t m_head{ 0 };
    std::uint64_t m_tail{ 0 };
};

class UploadManager
{
public:
    struct Desc
    {
        std::uint64_t stagingBufferSize{ 64 * 1024 * 1024 };      // Default 64 MB
        std::uint64_t overflowThreshold{ stagingBufferSize / 2 }; // Threshold for the overflow path
    };

    auto upload(const Buffer& dst, std::span<std::byte> data, std::size_t alignment = 4,
        std::size_t dstOffset = 0)
    {
        auto allocation = m_stagingBuffer.allocate(data.size(), alignment);
        if (!allocation)
            return; // Error

        std::memcpy(allocation->data + allocation->offset, data.data(), data.size());

        // TODO: Begin cmd if not already in progress

        m_cmd.copyBuffer(m_stagingBuffer.buffer(), dst, data.size(), allocation->offset, dstOffset);

        // TODO: Enqueue + save timeline value

        m_pendingBatch = Batch{
            .startOffset = allocation->offset,
            .endOffset = allocation->offset + allocation->size,
        };
    }

    auto upload(const Image& dst, std::span<std::byte> data)
    {
        auto allocation = m_stagingBuffer.allocate(data.size(), 4);
        if (!allocation)
            return; // Error

        std::memcpy(allocation->data, data.data(), data.size());

        // TODO: Begin cmd if not already in progress

        // TODO: Transition Image Layout to Transition Dst optimal

        // TODO: Record copy for each mip / layer

        // TODO: Transition image layout again

        m_pendingBatch = Batch{
            .startOffset = allocation->offset,
            .endOffset = allocation->offset + allocation->size,
        };
    }

    auto flushPending(Queue& queue) -> std::optional<TimelineValue>
    {
        if (m_inProgressBatch)
        {
            queue.wait(m_inProgressBatch->completeTime);
            m_stagingBuffer.reclaim(m_inProgressBatch->batch.endOffset);
            m_inProgressBatch = std::nullopt;
        }

        if (!m_pendingBatch)
            return std::nullopt;

        m_cmd.end();

        auto timelineValue = queue.submit(m_cmd);
        if (!timelineValue)
            return std::nullopt;

        m_inProgressBatch = UploadBatch{
            .completeTime = *timelineValue,
            .batch = *m_pendingBatch,
        };

        return *timelineValue;
    }

private:
    struct Batch
    {
        std::size_t startOffset;
        std::size_t endOffset;
    };

    struct UploadBatch
    {
        TimelineValue completeTime;
        Batch batch;
    };

    vk::raii::CommandPool m_cmdPool;
    CommandBuffer m_cmd;
    StagingBuffer m_stagingBuffer;
    std::optional<Batch> m_pendingBatch;
    std::optional<UploadBatch> m_inProgressBatch;
};
}
