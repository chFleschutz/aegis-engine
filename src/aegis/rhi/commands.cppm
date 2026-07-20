module;
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

export module aegis.rhi:commands;
import :common;
import :resource_handle;

export namespace aegis::rhi
{
struct ImageLayoutTransition
{
    ImageViewHandle imageView;
    ResourceState oldState;
    ResourceState newState;
};

struct Attachment
{
    ImageViewHandle imageView;
    AttachmentLoadOp loadOp;
    AttachmentStoreOp storeOp;
    std::optional<ClearValue> clearValue;

    static auto color(ImageViewHandle view, ClearColor clear) -> Attachment;
    static auto colorLoad(ImageViewHandle view) -> Attachment;
    static auto depth(ImageViewHandle view, ClearDepthStencil clear) -> Attachment;
    static auto depthReadOnly(ImageViewHandle view) -> Attachment;
};

/// @brief One buffer <-> image copy region: a whole mip level, across 'arrayLayerCount' layers.
/// @note 'bufferOffset' must be a multiple of 4 and of the format's texel size; detail::
///       subresourceFootprints already lays offsets out that way.
struct BufferImageCopy
{
    std::size_t bufferOffset{ 0 };
    std::uint32_t mipLevel{ 0 };
    std::uint32_t baseArrayLayer{ 0 };
    std::uint32_t arrayLayerCount{ 1 };
    Extent3D extent;
};

struct RenderingCmd
{
    std::string_view name;
    std::span<Attachment> colorAttachments;
    std::optional<Attachment> depthAttachment;
};
}
