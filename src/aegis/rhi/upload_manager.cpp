module;
#include <expected>
#include <utility>
#include <vector>

module aegis.rhi;
import :upload_manager;
import :buffer;
import :command_buffer;
import :command_pool;
import :common;
import :device;
import :error;
import :resource_handle;

namespace aegis::rhi
{
StagingBuffer::StagingBuffer(StagingBuffer&& other) noexcept :
    m_device{ other.m_device },
    m_handle{ other.m_handle },
    m_data{ other.m_data },
    m_size{ other.m_size },
    m_head{ other.m_head },
    m_tail{ other.m_tail }
{
    other.m_device = nullptr;
}

StagingBuffer::~StagingBuffer()
{
    if (m_device)
        m_device->free(m_handle);
}

auto StagingBuffer::operator=(StagingBuffer&& other) noexcept -> StagingBuffer&
{
    if (this != &other)
    {
        if (m_device)
            m_device->free(m_handle);

        m_device = other.m_device;
        m_handle = other.m_handle;
        m_data = other.m_data;
        m_size = other.m_size;
        m_head = other.m_head;
        m_tail = other.m_tail;
        other.m_device = nullptr;
    }
    return *this;
}

auto StagingBuffer::create(Device& device, std::size_t size) -> std::expected<StagingBuffer, Error>
{
    auto handle = device.createBuffer({
        .name = "UploadManager staging",
        .size = size,
        .usage = BufferUsage::TransferSrc,
        .memory = MemoryUsage::CpuWrite,
    });
    if (!handle)
        return std::unexpected{ handle.error() };

    // Persistently mapped by VMA for CpuWrite memory; the pointer stays valid for the buffer's life.
    auto* data = device.get(*handle).data();
    return StagingBuffer{ device, *handle, data, size };
}

auto StagingBuffer::buffer() const -> const Buffer&
{
    return m_device->get(m_handle);
}

auto StagingBuffer::allocate(std::size_t size, std::size_t alignment) -> std::expected<Allocation, AllocError>
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

StagingBuffer::StagingBuffer(Device& device, BufferHandle handle, std::byte* data, std::size_t size) :
    m_device{ &device },
    m_handle{ handle },
    m_data{ data },
    m_size{ size }
{
}

auto StagingBuffer::isRegionFree(std::size_t start, std::size_t end) const -> bool

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

auto UploadManager::create(Device& device, std::uint32_t queueFamily, const Desc& desc)
    -> std::expected<UploadManager, Error>
{
    auto pool = device.createCommandPool({ .name = "UploadManager", .queueFamily = queueFamily });
    if (!pool)
        return std::unexpected{ pool.error() };

    std::vector<CommandBuffer> cmds;
    cmds.reserve(desc.framesInFlight);
    for (std::uint32_t i = 0; i < desc.framesInFlight; ++i)
    {
        auto cmd = device.createCommandBuffer({ .name = "UploadManager", .pool = *pool });
        if (!cmd)
            return std::unexpected{ cmd.error() };
        cmds.emplace_back(std::move(*cmd));
    }

    auto staging = StagingBuffer::create(device, desc.stagingBufferSize);
    if (!staging)
        return std::unexpected{ staging.error() };

    return UploadManager{ std::move(*pool), std::move(cmds), std::move(*staging) };
}

auto UploadManager::upload(const Buffer& dst, std::span<const std::byte> data, std::size_t alignment,
    std::size_t dstOffset) -> bool

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

auto UploadManager::upload(const Image& dst, std::span<const std::byte> data) -> bool
{
    // TODO: transition layout to transfer-dst optimal, record a copy per mip/layer, transition
    //       back (and optionally generate mipmaps), mirroring the buffer batch lifecycle.
    (void) dst;
    (void) data;
    return false;
}

auto UploadManager::flushPending(Queue& queue) -> std::optional<TimelineValue>

{
    if (!m_batchOpen)
    {
        // Nothing to submit. Drain the oldest in-flight batch so a caller flushing to
        // free staging space (e.g. after upload() returned false) makes forward progress.
        if (!m_inFlight.empty())
            reclaimFront(queue);
        return std::nullopt;
    }

    auto& cmd = m_cmds[m_recordIndex];
    cmd.end();

    m_batchOpen = false;

    auto timelineValue = queue.submit(cmd);
    if (!timelineValue)
        return std::nullopt;

    m_inFlight.emplace_back(InFlight{
        .completeTime = *timelineValue,
        .endOffset = m_pendingEndOffset,
    });
    m_recordIndex = (m_recordIndex + 1) % m_cmds.size();

    // Keep at most framesInFlight-1 batches outstanding so the command buffer the next batch
    // records into is free to reset. This is the only blocking point on the happy path, and it
    // only waits when the GPU is a full ring of command buffers behind.
    while (m_inFlight.size() >= m_cmds.size())
        reclaimFront(queue);

    return *timelineValue;
}

UploadManager::UploadManager(CommandPool cmdPool, std::vector<CommandBuffer> cmds,
    StagingBuffer stagingBuffer) :
    m_cmdPool{ std::move(cmdPool) },
    m_cmds{ std::move(cmds) },
    m_stagingBuffer{ std::move(stagingBuffer) }
{
}

auto UploadManager::reclaimFront(const Queue& queue) -> void
{
    const auto& [completeTime, endOffset] = m_inFlight.front();
    std::ignore = queue.wait(completeTime);
    m_stagingBuffer.reclaim(endOffset);
    m_inFlight.pop_front();
}
}
