module;
#include <cstdint>

export module aegis.rhi:commands;
import :common;
import :image_ref;

import vulkan_hpp; //< TODO: Remove

export namespace aegis::rhi
{
struct ImageLayoutTransition
{
    ImageRef image;
    ResourceState oldState;
    ResourceState newState;
    // TODO: Hide aspect flags in rhi image or derive from image format
    vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor;
    std::uint32_t baseMipLevel = 0;
    std::uint32_t levelCount = 1;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t layerCount = 1;
};
}
