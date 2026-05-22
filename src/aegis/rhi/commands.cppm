module;
#include <optional>
#include <span>

export module aegis.rhi:commands;
import :common;
import :image_ref;

export namespace aegis::rhi
{
struct ImageLayoutTransition
{
    ImageRef imageRef;
    ResourceState oldState;
    ResourceState newState;
};

struct Attachment
{
    ImageRef image;
    AttachmentLoadOp loadOp;
    AttachmentStoreOp storeOp;
    std::optional<ClearValue> clearValue;

    static auto color(const ImageRef& image, ClearColor clear) -> Attachment;
    static auto colorLoad(const ImageRef& image) -> Attachment;
    static auto depth(const ImageRef& image, ClearDepthStencil clear) -> Attachment;
    static auto depthReadOnly(const ImageRef& image) -> Attachment;
};

struct RenderingCmd
{
    std::span<Attachment> colorAttachments;
    std::optional<Attachment> depthAttachment;
};
}
