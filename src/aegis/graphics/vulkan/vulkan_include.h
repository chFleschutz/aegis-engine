#pragma once

// Ensure only volk is used for vulkan function loading
// NEVER include <vulkan/vulkan.h> directly in any file, use this header instead
#define VK_NO_PROTOTYPES
#include <volk.h>

#include <vk_mem_alloc.h>

#include "core/assert.h"

// TODO: Replace this macro
#define VK_CHECK(f)													\
{																	\
	VkResult vkResult = (f);										\
	AGX_ASSERT_X(vkResult == VK_SUCCESS, "Vulkan Error: '" #f "'"); \
}
