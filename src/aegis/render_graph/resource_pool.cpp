module;
#include <cassert>

module aegis.render_graph;
import std;
import aegis.renderer;
import aegis.base;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// Per-frame copies start on this boundary; generous, since shaders reach them by address.
constexpr std::size_t perFrameAlignment{ 256 };
}

ResourcePool::ResourcePool(rhi::Device& device, renderer::Renderer& renderer, const ResourceDeclarations& declarations) :
    m_device{ device },
    m_renderer{ renderer },
    m_declarations{ declarations }
{
}

ResourcePool::~ResourcePool()
{
    for (const auto& created : m_created)
    {
        if (const auto* texture = std::get_if<CreatedTexture>(&created); texture && texture->ownedByPool)
        {
            m_device.free(texture->texture.view);
            m_device.free(texture->texture.image);
        }
        else if (const auto* buffer = std::get_if<CreatedBuffer>(&created))
        {
            m_device.free(buffer->buffer);
        }
    }
}

auto ResourcePool::actual(ResourceHandle handle) const -> std::uint32_t
{
    assert(handle.index < m_targets.size() && "Invalid frame graph resource handle, or used before compile");
    return m_targets[handle.index];
}

auto ResourcePool::texture(ResourceHandle handle) const -> const renderer::Texture&
{
    const auto* created = std::get_if<CreatedTexture>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a texture");
    return created->texture;
}

auto ResourcePool::storageDescriptor(ResourceHandle handle) const -> rhi::StorageImageDescriptor
{
    return *m_device.get(texture(handle).view).storageDescriptor();
}

auto ResourcePool::format(ResourceHandle handle) const -> rhi::Format
{
    const auto* desc = std::get_if<TextureDesc>(&m_declarations.entries()[actual(handle)].info);
    assert(desc && "Frame graph resource is not a texture");
    return desc->format;
}

auto ResourcePool::extent(ResourceHandle handle) const -> rhi::Extent2D
{
    return m_device.get(texture(handle).image).extent().toExtent2D();
}

auto ResourcePool::buffer(ResourceHandle handle) const -> const rhi::Buffer&
{
    const auto* created = std::get_if<CreatedBuffer>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a buffer");
    return m_device.get(created->buffer);
}

auto ResourcePool::bufferOffset(ResourceHandle handle, std::uint32_t frameIndex) const -> std::size_t
{
    const auto* created = std::get_if<CreatedBuffer>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a buffer");
    return created->stride * frameIndex;
}

auto ResourcePool::bufferAddress(ResourceHandle handle, std::uint32_t frameIndex) const -> rhi::DeviceAddress
{
    return buffer(handle).deviceAddress() + bufferOffset(handle, frameIndex);
}

auto ResourcePool::write(ResourceHandle handle, std::uint32_t frameIndex, std::span<const std::byte> data,
    std::size_t offset) const -> void
{
    if (data.empty())
        return;
    buffer(handle).write(data.data(), data.size(), bufferOffset(handle, frameIndex) + offset);
}

auto ResourcePool::bufferSize(ResourceHandle handle) const -> std::size_t
{
    const auto* created = std::get_if<CreatedBuffer>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a buffer");
    return created->size;
}

auto ResourcePool::resizeBuffer(ResourceHandle handle, std::size_t size) -> std::expected<void, Error>
{
    const auto target = actual(handle);
    auto* created = std::get_if<CreatedBuffer>(&m_created[target]);
    const auto* desc = std::get_if<BufferDesc>(&m_declarations.entries()[target].info);
    assert(desc && created && "Frame graph resource is not a created buffer");

    auto resized = createBuffer(m_declarations.entries()[target].name, { .size = size, .perFrame = desc->perFrame },
        created->usage, created->memory);
    if (!resized)
        return std::unexpected{ resized.error() };

    m_device.free(created->buffer);
    *created = *resized;
    return {};
}

auto ResourcePool::createBuffer(const std::string& name, const BufferDesc& desc, rhi::BufferUsage usage,
    rhi::MemoryUsage memory) const -> std::expected<CreatedBuffer, Error>
{
    constexpr auto frames = renderer::Renderer::maxFramesInFlight;
    const auto stride = desc.perFrame ? base::alignTo(desc.size, perFrameAlignment) : 0;
    auto buffer = m_device.createBuffer({
        .name = name,
        .size = desc.perFrame ? stride * frames : desc.size,
        .usage = usage,
        .memory = memory,
    });
    if (!buffer)
        return makeError(ErrorCode::ResourceCreationFailed, std::format("Failed to create frame graph buffer '{}'", name), buffer.error());
    return CreatedBuffer{ *buffer, stride, usage, memory, desc.size };
}

auto ResourcePool::createResources(const GraphPlan& plan) -> std::expected<void, Error>
{
    const auto entries = m_declarations.entries();
    m_targets = plan.target;
    m_created.resize(entries.size());
    for (std::uint32_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        if (const auto* desc = std::get_if<TextureDesc>(&entry.info))
        {
            const bool resolutionDependent = desc->extent == rhi::Extent2D{ 0, 0 };
            const renderer::Texture::Desc textureDesc{
                .name = entry.name,
                .extent = rhi::Extent3D{ resolutionDependent ? m_renderer.extent() : desc->extent },
                .format = desc->format,
                .usage = plan.imageUsage[i],
                .mipLevels = desc->mipLevels,
            };
            auto texture = resolutionDependent ? m_renderer.createResolutionDependentTexture(textureDesc)
                                               : m_renderer.createTexture(textureDesc);
            if (!texture)
                return makeError(ErrorCode::ResourceCreationFailed, std::format("Failed to create frame graph texture '{}'", entry.name), texture.error());
            m_created[i] = CreatedTexture{ *texture, !resolutionDependent };
        }
        else if (const auto* desc = std::get_if<BufferDesc>(&entry.info))
        {
            auto buffer = createBuffer(entry.name, *desc,
                rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress | plan.bufferUsage[i],
                plan.hostWritten[i] ? rhi::MemoryUsage::CpuWrite : rhi::MemoryUsage::GpuOnly);
            if (!buffer)
                return std::unexpected{ buffer.error() };
            m_created[i] = *buffer;
        }
    }
    return {};
}
}
