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

auto UploadManager::create(Device& device, std::uint32_t queueFamily)
    -> std::expected<UploadManager, Error>
{
    return create(device, queueFamily, Desc{});
}

auto UploadManager::create(Device& device, std::uint32_t queueFamily, const Desc& desc)
    -> std::expected<UploadManager, Error>
{
    auto pool = device.createCommandPool({ .name = "UploadManager", .queueFamily = queueFamily });
    if (!pool)
        return std::unexpected{ pool.error() };

    std::vector<CommandBuffer> cmds;
    cmds.reserve(framesInFlight);
    for (std::uint32_t i = 0; i < framesInFlight; ++i)
    {
        auto cmd = device.createCommandBuffer({ .name = "UploadManager", .pool = *pool });
        if (!cmd)
            return std::unexpected{ cmd.error() };
        cmds.push_back(std::move(*cmd));
    }

    auto staging = StagingBuffer::create(device, desc.stagingBufferSize);
    if (!staging)
        return std::unexpected{ staging.error() };

    return UploadManager{ std::move(*pool), std::move(cmds), std::move(*staging) };
}

UploadManager::UploadManager(CommandPool cmdPool, std::vector<CommandBuffer> cmds,
    StagingBuffer stagingBuffer) :
    m_cmdPool{ std::move(cmdPool) },
    m_cmds{ std::move(cmds) },
    m_stagingBuffer{ std::move(stagingBuffer) }
{
}
}
