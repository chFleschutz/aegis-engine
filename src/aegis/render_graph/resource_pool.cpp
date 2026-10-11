module;
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module aegis.render_graph;
import aegis.renderer;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// Per-frame copies start on this boundary; generous, since shaders reach them by address.
constexpr std::size_t perFrameAlignment{ 256 };
}

ResourcePool::ResourcePool(rhi::Device& device, renderer::Renderer& renderer) :
    m_device{ device },
    m_renderer{ renderer }
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

auto ResourcePool::addTexture(std::string name, Usage usage, const TextureDesc& desc) -> ResourceHandle
{
    m_entries.push_back({ std::move(name), usage, desc });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
}

auto ResourcePool::addBuffer(std::string name, Usage usage, const BufferDesc& desc) -> ResourceHandle
{
    m_entries.push_back({ std::move(name), usage, desc });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
}

auto ResourcePool::addReference(std::string name, Usage usage) -> ResourceHandle
{
    m_entries.push_back({ std::move(name), usage, Reference{} });
    return { static_cast<std::uint32_t>(m_entries.size() - 1) };
}

auto ResourcePool::actual(ResourceHandle handle) const -> std::uint32_t
{
    assert(handle.index < m_entries.size() && "Invalid frame graph resource handle");
    if (const auto* reference = std::get_if<Reference>(&m_entries[handle.index].info))
    {
        assert(reference->target && "Frame graph reference used before compile");
        return *reference->target;
    }
    return handle.index;
}

auto ResourcePool::texture(ResourceHandle handle) const -> const renderer::Texture&
{
    const auto* created = std::get_if<CreatedTexture>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a texture");
    return created->texture;
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
    buffer(handle).write(data.data(), data.size(), bufferOffset(handle, frameIndex) + offset);
}

auto ResourcePool::resolveReferences() -> std::expected<void, Error>
{
    for (auto& entry : m_entries)
    {
        auto* reference = std::get_if<Reference>(&entry.info);
        if (!reference)
            continue;

        for (std::uint32_t i = 0; i < m_entries.size(); ++i)
        {
            if (!std::holds_alternative<Reference>(m_entries[i].info) && m_entries[i].name == entry.name)
            {
                reference->target = i;
                break;
            }
        }
        if (!reference->target)
            return makeError(std::format("Frame graph resource '{}' is referenced but never created", entry.name));
    }
    return {};
}

auto ResourcePool::createResources() -> std::expected<void, Error>
{
    using namespace rhi::utility;

    // Every access contributes usage flags to the resource it touches.
    std::vector<rhi::ImageUsage> imageUsage(m_entries.size(), rhi::ImageUsage::None);
    std::vector<rhi::BufferUsage> bufferUsage(m_entries.size(), rhi::BufferUsage::None);
    std::vector<bool> hostWritten(m_entries.size(), false);
    for (std::uint32_t i = 0; i < m_entries.size(); ++i)
    {
        const auto target = actual({ i });
        imageUsage[target] = imageUsage[target] | detail::toImageUsage(m_entries[i].usage);
        bufferUsage[target] = bufferUsage[target] | detail::toBufferUsage(m_entries[i].usage);
        hostWritten[target] = hostWritten[target] || m_entries[i].usage == Usage::HostWrite;
    }

    constexpr auto frames = renderer::Renderer::maxFramesInFlight;
    m_created.resize(m_entries.size());
    for (std::uint32_t i = 0; i < m_entries.size(); ++i)
    {
        const auto& entry = m_entries[i];
        if (const auto* desc = std::get_if<TextureDesc>(&entry.info))
        {
            const bool resolutionDependent = desc->extent == rhi::Extent2D{ 0, 0 };
            const renderer::Texture::Desc textureDesc{
                .name = entry.name,
                .extent = rhi::Extent3D{ resolutionDependent ? m_renderer.extent() : desc->extent },
                .format = desc->format,
                .usage = imageUsage[i],
                .mipLevels = desc->mipLevels,
            };
            auto texture = resolutionDependent ? m_renderer.createResolutionDependentTexture(textureDesc)
                                               : m_renderer.createTexture(textureDesc);
            if (!texture)
                return makeError(texture.error(), std::format("Failed to create frame graph texture '{}'", entry.name));
            m_created[i] = CreatedTexture{ *texture, !resolutionDependent };
        }
        else if (const auto* desc = std::get_if<BufferDesc>(&entry.info))
        {
            const auto stride = desc->perFrame ? alignTo(desc->size, perFrameAlignment) : 0;
            auto buffer = m_device.createBuffer({
                .name = entry.name,
                .size = desc->perFrame ? stride * frames : desc->size,
                .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress | bufferUsage[i],
                .memory = hostWritten[i] ? rhi::MemoryUsage::CpuWrite : rhi::MemoryUsage::GpuOnly,
            });
            if (!buffer)
                return makeError(buffer.error(), std::format("Failed to create frame graph buffer '{}'", entry.name));
            m_created[i] = CreatedBuffer{ *buffer, stride };
        }
    }
    return {};
}
}
