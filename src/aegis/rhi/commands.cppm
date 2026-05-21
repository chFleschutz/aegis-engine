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
    ImageRef imageRef;
    ResourceState oldState;
    ResourceState newState;
    // TODO: Hide aspect flags in rhi image or derive from image format
    vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor;
};
}
