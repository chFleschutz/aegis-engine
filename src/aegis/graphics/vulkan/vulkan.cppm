module;

#define VK_NO_PROTOTYPES
#include <volk.h>

#include <vk_mem_alloc.h>

export module Aegis.Graphics.Vulkan;

export 
{
	using ::VkInstance;
	using ::VkDevice;
	using ::VkCommandBuffer;
	using ::VkResult;
	using ::VkBuffer;
	using ::VkImage;
	using ::VkImageView;
	using ::VkSampler;
	using ::VkPipeline;
	using ::VkPipelineLayout;
	using ::VkDescriptorSet;
	using ::VkDescriptorPool;
	using ::VkDescriptorSetLayout;
	using ::VkDescriptorBufferInfo;
	using ::VkDescriptorImageInfo;

	using ::vkDestroyImageView;
	using ::vkDestroySampler;
	using ::vkDestroyPipeline;

	using ::VkDebugUtilsMessengerEXT;
	using ::VkDebugUtilsMessengerCreateInfoEXT;
	using ::VkDebugUtilsMessageSeverityFlagBitsEXT;
	using ::VkDebugUtilsMessageTypeFlagBitsEXT;
	using ::vkCreateDebugUtilsMessengerEXT;
	using ::vkDestroyDebugUtilsMessengerEXT;

	// TODO: Add all needed Vulkan types here

	using ::VmaAllocation;
	using ::vmaDestroyBuffer;
	using ::vmaDestroyImage;
}
