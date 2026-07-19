module;
#include <expected>

export module aegis.rhi:ring_allocator;
import :utility;

export namespace aegis::rhi
{
/// @brief FIFO ring allocator handing out byte offsets into a fixed-size region.
/// @note Pure offset arithmetic -- it owns no memory and knows nothing about the region it carves
///       up. Sub-allocations are handed out in FIFO order and must be reclaimed once the consumer
///       no longer needs them. See StagingBuffer for the GPU-facing user.
class RingAllocator
{
public:
    enum class AllocError
    {
        ZeroSizeAllocation,
        RingSizeExceeded,
        MemoryExhausted,
    };

    explicit RingAllocator(std::size_t size) :
        m_size{ size }
    {
    }

    /// @brief Reserves 'size' bytes aligned to 'alignment' from the ring. Never blocks.
    /// @return The offset of the reserved region, or an Error if the ring cannot allocate it.
    [[nodiscard]] auto allocate(std::size_t size, std::size_t alignment)
        -> std::expected<std::size_t, AllocError>
    {
        if (size == 0)
            return std::unexpected{ AllocError::ZeroSizeAllocation };

        if (size > m_size)
            return std::unexpected{ AllocError::RingSizeExceeded };

        const auto alignedHead = utility::alignTo(m_head, alignment);
        if (alignedHead + size <= m_size)
        {
            // Fits before capacity -> no wrap-around
            if (!isRegionFree(alignedHead, alignedHead + size))
                return std::unexpected{ AllocError::MemoryExhausted };

            m_head = alignedHead + size;
            return alignedHead;
        }

        // Does not fit before capacity -> wrap-around to the front
        if (!isRegionFree(0, size))
            return std::unexpected{ AllocError::MemoryExhausted };

        m_head = size;
        return std::size_t{ 0 };
    }

    /// @brief Frees everything up to (and including) the given allocation end offset.
    auto reclaim(std::size_t allocEndOffset) -> void { m_tail = allocEndOffset; }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_size; }

private:
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

    std::size_t m_size;
    std::size_t m_head{ 0 };
    std::size_t m_tail{ 0 };
};
}
