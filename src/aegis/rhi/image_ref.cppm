module;
#include <cstdint>

export module aegis.rhi:image_ref;
import :error;
import vulkan_hpp;

export namespace aegis::rhi
{
struct ImageRef
{
    vk::Image image;
    vk::ImageView view;
    Extent3D extent;
    Format format{ Format::Unknown };
    std::uint32_t baseMipLevel{ 0 };
    std::uint32_t levelCount{ 1 };
    std::uint32_t baseArrayLayer{ 0 };
    std::uint32_t layerCount{ 1 };
};
}
