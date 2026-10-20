export module aegis.render_graph:resource;
import std;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief How a pass accesses a graph resource. Determines the barriers between passes and, merged
///        over all accesses, the usage flags the resource is created with.
enum class Usage
{
    ColorAttachment, // Image, written as a color attachment
    DepthAttachment, // Image, depth tested and written
    DepthRead,       // Image, depth tested only (read-only attachment)
    SampledFragment, // Image, sampled in a fragment shader
    SampledCompute,  // Image, sampled in a compute shader
    StorageRead,     // Image or buffer, read as storage by a compute shader
    StorageWrite,    // Image or buffer, written as storage by a compute shader
    ShaderRead,      // Buffer, read through its device address by any shader stage
    IndirectRead,    // Buffer, read as indirect draw arguments
    TransferSrc,     // Image or buffer, copied or blitted from
    TransferDst,     // Image or buffer, copied, blitted or filled into
    HostWrite,       // Buffer, written by the CPU through its mapping; no GPU access
};

/// @brief A resource declaration of one pass: an entry in the ResourcePool.
struct ResourceHandle
{
    static constexpr std::uint32_t invalid{ std::numeric_limits<std::uint32_t>::max() };

    std::uint32_t index{ invalid };

    [[nodiscard]] auto isValid() const noexcept -> bool { return index != invalid; }
    auto operator==(const ResourceHandle&) const -> bool = default;
};

struct TextureDesc
{
    rhi::Format format;
    /// @brief {0, 0}: the renderer's extent, recreated on resize. Otherwise a fixed size.
    rhi::Extent2D extent{ 0, 0 };
    std::uint32_t mipLevels{ 1 };
    /// @brief Usage beyond what the graph accesses imply, e.g. Sampled for mips a pass samples itself.
    rhi::ImageUsage usage{ rhi::ImageUsage::None };
};

struct BufferDesc
{
    std::size_t size;
    /// @brief One copy per frame in flight, so a frame's writes never race the GPU reading the
    ///        previous frame's. Needed for anything the CPU writes every frame.
    bool perFrame{ false };
};
}

export namespace aegis::render_graph::detail
{
[[nodiscard]] constexpr auto isWrite(Usage usage) noexcept -> bool
{
    switch (usage)
    {
    case Usage::ColorAttachment:
    case Usage::DepthAttachment:
    case Usage::StorageWrite:
    case Usage::TransferDst:
    case Usage::HostWrite:
        return true;
    case Usage::DepthRead:
    case Usage::SampledFragment:
    case Usage::SampledCompute:
    case Usage::StorageRead:
    case Usage::ShaderRead:
    case Usage::IndirectRead:
    case Usage::TransferSrc:
        return false;
    }
    return false;
}

/// @brief The image layout a usage needs, as a class; equal classes need no layout transition.
enum class LayoutClass
{
    Attachment,
    ReadOnly,
    General,
    TransferSrc,
    TransferDst,
    None, // Buffer-only usages
};

[[nodiscard]] constexpr auto layoutClass(Usage usage) noexcept -> LayoutClass
{
    switch (usage)
    {
    case Usage::ColorAttachment:
    case Usage::DepthAttachment:
        return LayoutClass::Attachment;
    case Usage::DepthRead:
    case Usage::SampledFragment:
    case Usage::SampledCompute:
        return LayoutClass::ReadOnly;
    case Usage::StorageRead:
    case Usage::StorageWrite:
        return LayoutClass::General;
    case Usage::TransferSrc:
        return LayoutClass::TransferSrc;
    case Usage::TransferDst:
        return LayoutClass::TransferDst;
    case Usage::ShaderRead:
    case Usage::IndirectRead:
    case Usage::HostWrite:
        return LayoutClass::None;
    }
    return LayoutClass::None;
}

[[nodiscard]] constexpr auto toResourceState(Usage usage) noexcept -> rhi::ResourceState
{
    switch (usage)
    {
    case Usage::ColorAttachment: return rhi::ResourceState::ColorAttachment;
    case Usage::DepthAttachment: return rhi::ResourceState::DepthWrite;
    case Usage::DepthRead: return rhi::ResourceState::DepthRead;
    case Usage::SampledFragment: return rhi::ResourceState::ShaderReadFragment;
    case Usage::SampledCompute: return rhi::ResourceState::ShaderReadCompute;
    case Usage::StorageRead:
    case Usage::StorageWrite: return rhi::ResourceState::ComputeStorage;
    case Usage::ShaderRead: return rhi::ResourceState::ShaderRead;
    case Usage::IndirectRead: return rhi::ResourceState::IndirectRead;
    case Usage::TransferSrc: return rhi::ResourceState::CopySrc;
    case Usage::TransferDst: return rhi::ResourceState::CopyDst;
    case Usage::HostWrite: return rhi::ResourceState::Unknown; // Never part of a barrier
    }
    return rhi::ResourceState::Unknown;
}

[[nodiscard]] constexpr auto toImageUsage(Usage usage) noexcept -> rhi::ImageUsage
{
    switch (usage)
    {
    case Usage::ColorAttachment: return rhi::ImageUsage::ColorAttachment;
    case Usage::DepthAttachment:
    case Usage::DepthRead: return rhi::ImageUsage::DepthStencilAttachment;
    case Usage::SampledFragment:
    case Usage::SampledCompute: return rhi::ImageUsage::Sampled;
    case Usage::StorageRead:
    case Usage::StorageWrite: return rhi::ImageUsage::Storage;
    case Usage::TransferSrc: return rhi::ImageUsage::TransferSrc;
    case Usage::TransferDst: return rhi::ImageUsage::TransferDst;
    case Usage::ShaderRead:
    case Usage::IndirectRead:
    case Usage::HostWrite: return rhi::ImageUsage::None;
    }
    return rhi::ImageUsage::None;
}

[[nodiscard]] constexpr auto toBufferUsage(Usage usage) noexcept -> rhi::BufferUsage
{
    switch (usage)
    {
    case Usage::IndirectRead: return rhi::BufferUsage::Indirect;
    case Usage::TransferSrc: return rhi::BufferUsage::TransferSrc;
    case Usage::TransferDst: return rhi::BufferUsage::TransferDst;
    case Usage::ColorAttachment:
    case Usage::DepthAttachment:
    case Usage::DepthRead:
    case Usage::SampledFragment:
    case Usage::SampledCompute:
    case Usage::StorageRead:
    case Usage::StorageWrite:
    case Usage::ShaderRead:
    case Usage::HostWrite: return rhi::BufferUsage::None;
    }
    return rhi::BufferUsage::None;
}
}

export namespace aegis::render_graph::detail
{
/// @brief The enumerator's name, e.g. "ColorAttachment", for tools, logs and plan snapshots.
[[nodiscard]] constexpr auto usageName(Usage usage) noexcept -> std::string_view
{
    switch (usage)
    {
    case Usage::ColorAttachment: return "ColorAttachment";
    case Usage::DepthAttachment: return "DepthAttachment";
    case Usage::DepthRead: return "DepthRead";
    case Usage::SampledFragment: return "SampledFragment";
    case Usage::SampledCompute: return "SampledCompute";
    case Usage::StorageRead: return "StorageRead";
    case Usage::StorageWrite: return "StorageWrite";
    case Usage::ShaderRead: return "ShaderRead";
    case Usage::IndirectRead: return "IndirectRead";
    case Usage::TransferSrc: return "TransferSrc";
    case Usage::TransferDst: return "TransferDst";
    case Usage::HostWrite: return "HostWrite";
    }
    return "Unknown";
}

/// @brief Whether an image can be accessed this way (false for the buffer-only usages).
[[nodiscard]] constexpr auto supportsImage(Usage usage) noexcept -> bool
{
    switch (usage)
    {
    case Usage::ColorAttachment:
    case Usage::DepthAttachment:
    case Usage::DepthRead:
    case Usage::SampledFragment:
    case Usage::SampledCompute:
    case Usage::StorageRead:
    case Usage::StorageWrite:
    case Usage::TransferSrc:
    case Usage::TransferDst:
        return true;
    case Usage::ShaderRead:
    case Usage::IndirectRead:
    case Usage::HostWrite:
        return false;
    }
    return false;
}

/// @brief Whether a buffer can be accessed this way (false for the image-only usages).
[[nodiscard]] constexpr auto supportsBuffer(Usage usage) noexcept -> bool
{
    switch (usage)
    {
    case Usage::StorageRead:
    case Usage::StorageWrite:
    case Usage::ShaderRead:
    case Usage::IndirectRead:
    case Usage::TransferSrc:
    case Usage::TransferDst:
    case Usage::HostWrite:
        return true;
    case Usage::ColorAttachment:
    case Usage::DepthAttachment:
    case Usage::DepthRead:
    case Usage::SampledFragment:
    case Usage::SampledCompute:
        return false;
    }
    return false;
}
}
