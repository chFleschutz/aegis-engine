module;
#include <cassert>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module aegis.rhi;
import :upload_manager;
import :buffer;
import :command_buffer;
import :command_pool;
import :commands;
import :common;
import :device;
import :error;
import :image;
import :resource_handle;

namespace aegis::rhi
{
StagingBuffer::StagingBuffer(StagingBuffer&& other) noexcept :
    m_device{ other.m_device },
    m_handle{ other.m_handle },
    m_data{ other.m_data },
    m_ring{ other.m_ring }
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
        m_ring = other.m_ring;
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
    return m_ring.allocate(size, alignment)
        .transform([&](std::size_t offset) {
            return Allocation{ .data = m_data, .offset = offset, .size = size };
        });
}

StagingBuffer::StagingBuffer(Device& device, BufferHandle handle, std::byte* data, std::size_t size) :
    m_device{ &device },
    m_handle{ handle },
    m_data{ data },
    m_ring{ size }
{
}

auto UploadManager::create(Device& device, std::uint32_t queueFamily, const Desc& desc)
    -> std::expected<UploadManager, Error>
{
    auto pool = device.createCommandPool({ .name = "UploadManager", .queueFamily = queueFamily });
    if (!pool)
        return std::unexpected{ pool.error() };

    std::vector<CommandBuffer> cmds;
    cmds.reserve(desc.maxOutstandingBatches);
    for (std::uint32_t i = 0; i < desc.maxOutstandingBatches; ++i)
    {
        auto cmd = device.createCommandBuffer({ .name = "UploadManager", .pool = *pool });
        if (!cmd)
            return std::unexpected{ cmd.error() };
        cmds.emplace_back(std::move(*cmd));
    }

    auto staging = StagingBuffer::create(device, desc.stagingBufferSize);
    if (!staging)
        return std::unexpected{ staging.error() };

    return UploadManager{ device, std::move(*pool), std::move(cmds), std::move(*staging) };
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

auto UploadManager::upload(ImageHandle dst, std::span<const std::byte> data, bool generateMips) -> bool
{
    const auto& image = m_device->get(dst);

    // With generateMips the caller supplies mip 0 only; the rest are blitted below.
    const auto sourceMips = generateMips ? std::uint32_t{ 1 } : image.mipLevels();
    const auto footprints = detail::subresourceFootprints(image.extent(), image.format(), sourceMips,
        image.arrayLayers());
    if (footprints.empty())
    {
        assert(false && "Image format cannot be uploaded from a linear buffer");
        return false;
    }

    const auto& last = footprints.back();
    // A size mismatch is a caller bug, not backpressure: returning false would send a retrying
    // caller into an infinite flush-and-retry loop, so trap it instead.
    assert(data.size() == last.sourceOffset + last.size && "Upload data does not match image layout");

    auto allocation = m_stagingBuffer.allocate(last.stagingOffset + last.size,
        detail::copyAlignment(image.format()));
    if (!allocation)
        return false;

    // Mip by mip rather than one memcpy: staging pads each mip up to the copy alignment, the
    // caller's data does not.
    for (const auto& footprint : footprints)
    {
        std::memcpy(allocation->data + allocation->offset + footprint.stagingOffset,
            data.data() + footprint.sourceOffset, footprint.size);
    }

    auto& cmd = m_cmds[m_recordIndex];
    if (!m_batchOpen)
    {
        // Safe to (re)record: a previous flush guaranteed this command buffer is not in flight.
        cmd.begin(true);
        m_batchOpen = true;
    }

    std::vector<BufferImageCopy> regions;
    regions.reserve(footprints.size());
    for (const auto& footprint : footprints)
    {
        regions.emplace_back(BufferImageCopy{
            .bufferOffset = allocation->offset + footprint.stagingOffset,
            .mipLevel = footprint.mipLevel,
            .baseArrayLayer = 0,
            .arrayLayerCount = footprint.arrayLayerCount,
            .extent = footprint.extent,
        });
    }

    cmd.transitionImageLayout(dst, ResourceState::Unknown, ResourceState::CopyDst);
    cmd.copyBufferToImage(m_stagingBuffer.buffer(), dst, regions);

    if (generateMips && image.mipLevels() > 1)
        cmd.generateMipmaps(dst, ResourceState::CopyDst);

    m_pendingEndOffset = allocation->offset + allocation->size;
    return true;
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

    // Keep at most maxOutstandingBatches-1 batches outstanding so the command buffer the next batch
    // records into is free to reset. This is the only blocking point on the happy path, and it
    // only waits when the GPU is a full ring of command buffers behind.
    while (m_inFlight.size() >= m_cmds.size())
        reclaimFront(queue);

    return *timelineValue;
}

UploadManager::UploadManager(Device& device, CommandPool cmdPool, std::vector<CommandBuffer> cmds,
    StagingBuffer stagingBuffer) :
    m_device{ &device },
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
