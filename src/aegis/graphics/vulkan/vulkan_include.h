#pragma once

// Ensure only volk is used for vulkan function loading
// NEVER include <vulkan/vulkan.h> directly in any file, use this header instead
#define VK_NO_PROTOTYPES
#include <volk.h>

// TODO: Replace this macro
#include "core/assert.h"
#define VK_CHECK(f)													\
{																	\
	VkResult vkResult = (f);										\
	AGX_ASSERT_X(vkResult == VK_SUCCESS, "Vulkan Error: '" #f "'"); \
}
