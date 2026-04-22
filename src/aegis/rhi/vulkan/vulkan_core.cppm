module;

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include <vk_mem_alloc.h>

export module aegis.rhi.vulkan:core;

// Vulkan export
export namespace vk {
    using vk::Instance;
}

// VMA export
export {
    using ::VmaAllocator;
}
