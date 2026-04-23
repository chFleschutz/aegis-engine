module;

#include <vk_mem_alloc.h>

export module aegis.rhi.vulkan:core;

export import vulkan_hpp;

// VMA export
export {
    using ::VmaAllocator;
}
