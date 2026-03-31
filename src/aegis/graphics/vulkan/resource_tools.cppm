module;

#include "graphics/vulkan/vulkan_include.h"

export module Aegis.Graphics.Vulkan.ResourceTools;

import Aegis.Graphics.Vulkan.Context;
import Aegis.Graphics.Vulkan.Tools;
import Aegis.Graphics.Texture;
import Aegis.Graphics.Image;
import Aegis.Graphics.Buffer;

export namespace Aegis::Graphics::Tools
{
	auto renderingAttachmentInfo(const Graphics::Texture& texture, VkAttachmentLoadOp loadOp, 
		VkClearValue clearValue = { 0.0f, 0.0f, 0.0f, 0.0f }) -> VkRenderingAttachmentInfo
	{
		return renderingAttachmentInfo(texture.view(), texture.image().layout(), loadOp, clearValue);
	}

	void setDebugUtilsObjectName(const Graphics::Buffer& buffer, const char* name)
	{
		if constexpr (Graphics::ENABLE_VALIDATION)
		{
			VkDebugUtilsObjectNameInfoEXT nameInfo{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
				.objectType = VK_OBJECT_TYPE_BUFFER,
				.objectHandle = reinterpret_cast<uint64_t>(buffer.buffer()),
				.pObjectName = name,
			};
			vkSetDebugUtilsObjectNameEXT(Graphics::VulkanContext::device(), &nameInfo);
		}
	}

	void setDebugUtilsObjectName(const Graphics::Image& image, const char* name)
	{
		if constexpr (Graphics::ENABLE_VALIDATION)
		{
			VkDebugUtilsObjectNameInfoEXT nameInfo{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
				.objectType = VK_OBJECT_TYPE_IMAGE,
				.objectHandle = reinterpret_cast<uint64_t>(image.image()),
				.pObjectName = name,
			};
			vkSetDebugUtilsObjectNameEXT(Graphics::VulkanContext::device(), &nameInfo);
		}
	}
}