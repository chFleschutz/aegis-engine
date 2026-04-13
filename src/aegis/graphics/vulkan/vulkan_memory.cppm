module;

#include "graphics/vulkan/vulkan_include.h"
#include <vk_mem_alloc.h>

export module Aegis.Graphics.Vulkan.VulkanMemory;

export namespace vma
{
	using Allocator = VmaAllocator;
	using AllocatorCreateInfo = VmaAllocatorCreateInfo;
	using Allocation = VmaAllocation;
	using AllocationCreateInfo = VmaAllocationCreateInfo;
	using AllocationCreateFlags = VmaAllocationCreateFlags;
	using AllocationInfo = VmaAllocationInfo;
	using VulkanFunctions = VmaVulkanFunctions;
	using MemoryUsage = VmaMemoryUsage;

	using AllocationCreateFlagBits = VmaAllocationCreateFlagBits;

	export using ::vmaCreateAllocator;
	export using ::vmaCreateBuffer;
	export using ::vmaCreateImage;

	export using ::vmaDestroyAllocator;
	export using ::vmaDestroyBuffer;
	export using ::vmaDestroyImage;

	export using ::vmaGetAllocationInfo;
	export using ::vmaMapMemory;
	export using ::vmaUnmapMemory;
	export using ::vmaCopyMemoryToAllocation;
	export using ::vmaFlushAllocation;

	export using ::vmaImportVulkanFunctionsFromVolk;
}

export
{
	using ::VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	using ::VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
	using ::VMA_ALLOCATION_CREATE_MAPPED_BIT;
	using ::VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
	using ::VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;
	using ::VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
	using ::VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
	using ::VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
	using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
	using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
	using ::VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
	using ::VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
	using ::VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT;
	using ::VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
	using ::VMA_ALLOCATION_CREATE_STRATEGY_FIRST_FIT_BIT;
	using ::VMA_ALLOCATION_CREATE_STRATEGY_MASK;
}
