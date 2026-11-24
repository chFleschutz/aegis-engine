module;
#include <cassert>

module aegis.render_graph;
import std;
import aegis.renderer;
import aegis.core;
import aegis.rhi;

namespace aegis::render_graph
{
namespace
{
// Per-frame copies start on this boundary; generous, since shaders reach them by address.
constexpr std::size_t perFrameAlignment{ 256 };
}

ResourcePool::ResourcePool(rhi::Device& device, const ResourceDeclarations& declarations, rhi::Extent2D output) :
    m_device{ device },
    m_declarations{ declarations },
    m_output{ output }
{
}

ResourcePool::~ResourcePool()
{
    for (const auto& created : m_created)
    {
        if (const auto* texture = std::get_if<CreatedTexture>(&created))
        {
            for (const auto& image : texture->textures)
            {
                if (image.image.isValid())
                    renderer::freeTexture(m_device, image);
            }
        }
        else if (const auto* buffer = std::get_if<CreatedBuffer>(&created))
        {
            m_device.free(buffer->buffer);
        }
    }
}

auto ResourcePool::actual(ResourceId handle) const -> std::uint32_t
{
    assert(handle.index < m_targets.size() && "Invalid frame graph resource handle, or used before compile");
    return m_targets[handle.index];
}

auto ResourcePool::textureAt(std::uint32_t entry, bool previous) const -> const renderer::Texture&
{
    if (const auto* imported = std::get_if<ImportedTexture>(&m_created[entry]))
    {
        assert(imported->texture && "Imported frame graph texture is not bound (FrameGraph::bind)");
        return *imported->texture;
    }
    const auto* created = std::get_if<CreatedTexture>(&m_created[entry]);
    assert(created && "Frame graph resource is not a texture");
    if (created->lifetime != Lifetime::History)
        return created->textures[0];
    const auto current = m_frame % 2;
    return created->textures[previous ? 1 - current : current];
}

auto ResourcePool::texture(ResourceId handle) const -> const renderer::Texture&
{
    return textureAt(actual(handle), m_previous[handle.index]);
}

auto ResourcePool::isFreshAt(std::uint32_t entry) const -> bool
{
    const auto* created = std::get_if<CreatedTexture>(&m_created[entry]);
    return created && created->lifetime != Lifetime::Frame && created->fresh;
}

auto ResourcePool::isFresh(ResourceId handle) const -> bool
{
    return isFreshAt(actual(handle));
}

auto ResourcePool::endFrame() -> void
{
    for (auto& created : m_created)
    {
        if (auto* texture = std::get_if<CreatedTexture>(&created))
            texture->fresh = false;
    }
    ++m_frame;
}

auto ResourcePool::bind(ResourceId handle, const renderer::Texture& texture) -> void
{
    auto* imported = std::get_if<ImportedTexture>(&m_created[actual(handle)]);
    assert(imported && "Frame graph resource is not an imported texture");
    imported->texture = texture;
}

auto ResourcePool::bind(ResourceId handle, rhi::ImageViewHandle view) -> void
{
    bind(handle, renderer::Texture{ .image = {}, .view = view });
}

auto ResourcePool::bind(ResourceId handle, rhi::BufferHandle buffer) -> void
{
    auto* imported = std::get_if<ImportedBuffer>(&m_created[actual(handle)]);
    assert(imported && "Frame graph resource is not an imported buffer");
    imported->buffer = buffer;
}

auto ResourcePool::storageDescriptor(ResourceId handle) const -> rhi::StorageImageDescriptor
{
    return *m_device.get(texture(handle).view).storageDescriptor();
}

auto ResourcePool::format(ResourceId handle) const -> rhi::Format
{
    const auto& info = m_declarations.entries()[actual(handle)].info;
    if (const auto* imported = std::get_if<ImportedTextureDesc>(&info))
        return imported->format;
    const auto* desc = std::get_if<TextureDesc>(&info);
    assert(desc && "Frame graph resource is not a texture");
    return desc->format;
}

auto ResourcePool::extent(ResourceId handle) const -> rhi::Extent2D
{
    const auto& found = texture(handle);
    // An imported image bound by its view alone (a swapchain image) has no image handle.
    if (!found.image.isValid())
        return m_device.get(found.view).extent().toExtent2D();
    return m_device.get(found.image).extent().toExtent2D();
}

auto ResourcePool::buffer(ResourceId handle) const -> const rhi::Buffer&
{
    if (const auto* imported = std::get_if<ImportedBuffer>(&m_created[actual(handle)]))
    {
        assert(imported->buffer && "Imported frame graph buffer is not bound (FrameGraph::bind)");
        return m_device.get(*imported->buffer);
    }
    const auto* created = std::get_if<CreatedBuffer>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a buffer");
    return m_device.get(created->buffer);
}

auto ResourcePool::bufferOffset(ResourceId handle, std::uint32_t frameIndex) const -> std::size_t
{
    if (std::holds_alternative<ImportedBuffer>(m_created[actual(handle)]))
        return 0;
    const auto* created = std::get_if<CreatedBuffer>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a buffer");
    return created->stride * frameIndex;
}

auto ResourcePool::bufferAddress(ResourceId handle, std::uint32_t frameIndex) const -> rhi::DeviceAddress
{
    return buffer(handle).deviceAddress() + bufferOffset(handle, frameIndex);
}

auto ResourcePool::write(ResourceId handle, std::uint32_t frameIndex, std::span<const std::byte> data,
    std::size_t offset) const -> void
{
    if (data.empty())
        return;
    buffer(handle).write(data.data(), data.size(), bufferOffset(handle, frameIndex) + offset);
}

auto ResourcePool::bufferSize(ResourceId handle) const -> std::size_t
{
    if (std::holds_alternative<ImportedBuffer>(m_created[actual(handle)]))
        return buffer(handle).size();
    const auto* created = std::get_if<CreatedBuffer>(&m_created[actual(handle)]);
    assert(created && "Frame graph resource is not a buffer");
    return created->size;
}

auto ResourcePool::resizeBuffer(ResourceId handle, std::size_t size) -> std::expected<void, Error>
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
    const auto stride = desc.perFrame ? core::alignTo(desc.size, perFrameAlignment) : 0;
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

auto ResourcePool::textureDesc(std::uint32_t entry, rhi::ImageUsage usage) const -> renderer::Texture::Desc
{
    const auto& declared = m_declarations.entries()[entry];
    const auto& desc = std::get<TextureDesc>(declared.info);
    return {
        .name = declared.name,
        .extent = rhi::Extent3D{ detail::textureExtent(desc, declared.scope ? scopeExtent(*declared.scope) : m_output) },
        .format = desc.format,
        .usage = usage,
        .mipLevels = desc.mipLevels,
    };
}

auto ResourcePool::scopeExtent(std::uint32_t scope) const -> rhi::Extent2D
{
    assert(scope < m_scopeExtents.size() && "Invalid frame graph scope, or used before compile");
    return m_scopeExtents[scope].value_or(m_output);
}

auto ResourcePool::resize(rhi::Extent2D output) -> std::expected<void, Error>
{
    m_output = output;
    // Textures of a scope with an extent of its own don't follow the output.
    return recreateFollowing([this](std::optional<std::uint32_t> scope) { return !scope || !m_scopeExtents[*scope]; });
}

auto ResourcePool::resize(std::uint32_t scope, rhi::Extent2D extent) -> std::expected<void, Error>
{
    assert(scope < m_scopeExtents.size() && "Invalid frame graph scope, or used before compile");
    m_scopeExtents[scope] = extent;
    return recreateFollowing([scope](std::optional<std::uint32_t> of) { return of == scope; });
}

auto ResourcePool::recreateFollowing(const std::function<bool(std::optional<std::uint32_t>)>& inScope) -> std::expected<void, Error>
{
    for (std::uint32_t i = 0; i < m_created.size(); ++i)
    {
        auto* created = std::get_if<CreatedTexture>(&m_created[i]);
        const auto& entry = m_declarations.entries()[i];
        if (!created || !detail::followsOutput(std::get<TextureDesc>(entry.info)) || !inScope(entry.scope))
            continue;
        for (std::size_t image = 0; image < (created->lifetime == Lifetime::History ? 2u : 1u); ++image)
        {
            if (auto recreated = renderer::recreateTexture(m_device, created->textures[image], textureDesc(i, created->usage)); !recreated)
                return makeError(ErrorCode::ResourceCreationFailed,
                    std::format("Failed to recreate frame graph texture '{}'", m_declarations.entries()[i].name), recreated.error());
        }
        created->fresh = true; // The new images hold nothing of the previous frame
    }
    return {};
}

auto ResourcePool::createResources(const GraphPlan& plan) -> std::expected<void, Error>
{
    const auto entries = m_declarations.entries();
    m_targets = plan.target;
    m_previous = plan.previous;
    m_scopeExtents.clear();
    for (const auto& scope : m_declarations.scopes())
        m_scopeExtents.push_back(scope.extent);
    m_created.resize(entries.size());
    for (std::uint32_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        if (const auto* desc = std::get_if<TextureDesc>(&entry.info))
        {
            // Stored image by image, so the destructor frees the first of a pair whose second failed.
            auto& created = m_created[i].emplace<CreatedTexture>(CreatedTexture{ .lifetime = desc->lifetime, .usage = plan.imageUsage[i] });
            for (std::size_t image = 0; image < (desc->lifetime == Lifetime::History ? 2u : 1u); ++image)
            {
                auto texture = renderer::createTexture(m_device, textureDesc(i, plan.imageUsage[i]));
                if (!texture)
                    return makeError(ErrorCode::ResourceCreationFailed, std::format("Failed to create frame graph texture '{}'", entry.name), texture.error());
                created.textures[image] = *texture;
            }
        }
        else if (std::holds_alternative<ImportedTextureDesc>(entry.info))
        {
            m_created[i] = ImportedTexture{};
        }
        else if (std::holds_alternative<ImportedBufferDesc>(entry.info))
        {
            m_created[i] = ImportedBuffer{};
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
