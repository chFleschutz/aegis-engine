module;
#include <cstdint>

export module aegis.rhi:commands;
import :common;

import vulkan_hpp; //< TODO: Remove

export namespace aegis::rhi
{
struct ImageLayoutTransition
{
    // TODO: Replace image with rhi type
    vk::Image image;
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
