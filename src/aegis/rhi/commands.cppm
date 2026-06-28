module;
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

struct RenderingCmd
{
    std::string_view name;
    std::span<Attachment> colorAttachments;
    std::optional<Attachment> depthAttachment;
};
}
