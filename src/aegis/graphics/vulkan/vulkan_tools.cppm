module;

#include "graphics/resources/texture.h"
#include "graphics/resources/buffer.h"
#include "graphics/vulkan/volk_include.h"

#include <string_view>

export module Aegis.Graphics.Vulkan.Tools;

import module Aegis.Graphics.Vulkan;

#define VK_CHECK(f)				\
{								\
	VkResult vkResult = (f);		\
	AGX_ASSERT_X(vkResult == VK_SUCCESS, "Vulkan Error: '" #f "'"); \
}

namespace Aegis::Tools
{
	/// @brief Returns the result as a string
	auto resultString(VkResult result) -> std::string_view
	{
		switch (result)
		{
#define CASE(name) case name: return #name
			CASE(VK_SUCCESS);
			CASE(VK_NOT_READY);
			CASE(VK_TIMEOUT);
			CASE(VK_EVENT_SET);
			CASE(VK_EVENT_RESET);
			CASE(VK_INCOMPLETE);
			CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
			CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
			CASE(VK_ERROR_INITIALIZATION_FAILED);
			CASE(VK_ERROR_DEVICE_LOST);
			CASE(VK_ERROR_MEMORY_MAP_FAILED);
			CASE(VK_ERROR_LAYER_NOT_PRESENT);
			CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
			CASE(VK_ERROR_FEATURE_NOT_PRESENT);
			CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
			CASE(VK_ERROR_TOO_MANY_OBJECTS);
			CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
			CASE(VK_ERROR_FRAGMENTED_POOL);
			CASE(VK_ERROR_UNKNOWN);
			CASE(VK_ERROR_OUT_OF_POOL_MEMORY);
			CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE);
			CASE(VK_ERROR_FRAGMENTATION);
			CASE(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
			CASE(VK_PIPELINE_COMPILE_REQUIRED);
			CASE(VK_ERROR_SURFACE_LOST_KHR);
			CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
			CASE(VK_SUBOPTIMAL_KHR);
			CASE(VK_ERROR_OUT_OF_DATE_KHR);
			CASE(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
			CASE(VK_ERROR_VALIDATION_FAILED_EXT);
			CASE(VK_ERROR_INVALID_SHADER_NV);
			CASE(VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR);
			CASE(VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR);
			CASE(VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR);
			CASE(VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR);
			CASE(VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR);
			CASE(VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR);
			CASE(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
			CASE(VK_ERROR_NOT_PERMITTED_KHR);
			CASE(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
			CASE(VK_THREAD_IDLE_KHR);
			CASE(VK_THREAD_DONE_KHR);
			CASE(VK_OPERATION_DEFERRED_KHR);
			CASE(VK_OPERATION_NOT_DEFERRED_KHR);
			CASE(VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR);
			CASE(VK_ERROR_COMPRESSION_EXHAUSTED_EXT);
			CASE(VK_INCOMPATIBLE_SHADER_BINARY_EXT);
#undef CASE
		default:
			return "RESULT_UNKNOWN";
		}
	}

	/// @brief Returns true if the format is a depth or depth-stencil format otherwise false
	auto isDepthFormat(VkFormat format) -> bool
	{
		switch (format)
		{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return true;
		default:
			return false;
		}
	}

	auto srcAccessMask(VkImageLayout layout) -> VkAccessFlags
	{
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			return 0;
		case VK_IMAGE_LAYOUT_GENERAL:
			return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return VK_ACCESS_SHADER_READ_BIT;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			return 0;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return VK_ACCESS_TRANSFER_READ_BIT;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return VK_ACCESS_TRANSFER_WRITE_BIT;
		default:
			AGX_ASSERT_X(false, "Unsupported layout transition");
			return 0;
		}
	}

	auto dstAccessMask(VkImageLayout layout) -> VkAccessFlags
	{
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			return 0;
		case VK_IMAGE_LAYOUT_GENERAL:
			return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return VK_ACCESS_SHADER_READ_BIT;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			return 0;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return VK_ACCESS_TRANSFER_READ_BIT;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return VK_ACCESS_TRANSFER_WRITE_BIT;
		default:
			AGX_ASSERT_X(false, "Unsupported layout transition");
			return 0;
		}
	}

	auto srcStage(VkAccessFlags access) -> VkPipelineStageFlags
	{
		if (access & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
			return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (access & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
			return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		if (access & VK_ACCESS_SHADER_WRITE_BIT)
			return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		if (access & VK_ACCESS_SHADER_READ_BIT)
			return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (access & VK_ACCESS_TRANSFER_READ_BIT)
			return VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (access & VK_ACCESS_TRANSFER_WRITE_BIT)
			return VK_PIPELINE_STAGE_TRANSFER_BIT;

		return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}

	auto dstStage(VkAccessFlags access) -> VkPipelineStageFlags
	{
		if (access & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT)
			return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (access & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
			return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		if (access & VK_ACCESS_SHADER_WRITE_BIT)
			return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		if (access & VK_ACCESS_SHADER_READ_BIT)
			return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (access & VK_ACCESS_TRANSFER_READ_BIT)
			return VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (access & VK_ACCESS_TRANSFER_WRITE_BIT)
			return VK_PIPELINE_STAGE_TRANSFER_BIT;

		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	auto aspectFlags(VkFormat format) -> VkImageAspectFlags
	{
		switch (format)
		{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		case VK_FORMAT_S8_UINT:
			return VK_IMAGE_ASPECT_STENCIL_BIT;
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		default:
			return VK_IMAGE_ASPECT_COLOR_BIT;  // For all color formats
		}
	}

	auto renderingAttachmentInfo(VkImageView imageView, VkImageLayout layout, VkAttachmentLoadOp loadOp, VkClearValue clearValue) -> VkRenderingAttachmentInfo
	{
		VkRenderingAttachmentInfo attachment{};
		attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		attachment.imageView = imageView;
		attachment.imageLayout = layout;
		attachment.loadOp = loadOp;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.clearValue = clearValue;
		return attachment;
	}

	auto renderingAttachmentInfo(const Graphics::Texture& texture, VkAttachmentLoadOp loadOp, VkClearValue clearValue = { 0.0f, 0.0f, 0.0f, 0.0f }) -> VkRenderingAttachmentInfo
	{
		return renderingAttachmentInfo(texture.view(), texture.image().layout(), loadOp, clearValue);
	}

	auto createShaderModule(VkDevice device, const std::vector<char>& code) -> VkShaderModule
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule = nullptr;
		VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

		return shaderModule;
	}

	auto createShaderModule(VkDevice device, const std::filesystem::path& path) -> VkShaderModule
	{
		auto code = File::readBinary(path);
		if (code.empty())
			ALOG::fatal("Failed to read shader file: {}", path.string());
		AGX_ASSERT_X(!code.empty(), "Shader code is empty");

		return createShaderModule(device, code);
	}

	auto createShaderStage(VkShaderStageFlagBits stage, VkShaderModule module, const char* entryPoint = "main")
		-> VkPipelineShaderStageCreateInfo
	{
		VkPipelineShaderStageCreateInfo shaderStage{};
		shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStage.stage = stage;
		shaderStage.module = module;
		shaderStage.pName = entryPoint;
		shaderStage.flags = 0;
		shaderStage.pNext = nullptr;
		shaderStage.pSpecializationInfo = nullptr;
		return shaderStage;
	}

	namespace vk
	{
		void cmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, VkExtent2D extent)
		{
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = { extent.width, extent.height, 1 };

			vkCmdCopyBufferToImage(cmd,
				buffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&region);
		}

		void cmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, VkExtent3D extent, uint32_t layerCount)
		{
			VkBufferImageCopy region{};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = layerCount;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = extent;

			vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &region);
		}

		void cmdDispatch(VkCommandBuffer cmd, uint32_t minThreads, uint32_t groupSize)
		{
			uint32_t groupCountX = (minThreads + groupSize - 1) / groupSize;
			vkCmdDispatch(cmd, groupCountX, 1, 1);
		}

		void cmdDispatch(VkCommandBuffer cmd, VkExtent2D minThreads, VkExtent2D groupSize)
		{
			uint32_t groupCountX = (minThreads.width + groupSize.width - 1) / groupSize.width;
			uint32_t groupCountY = (minThreads.height + groupSize.height - 1) / groupSize.height;
			vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
		}

		void cmdDispatch(VkCommandBuffer cmd, VkExtent3D minThreads, VkExtent3D groupSize)
		{
			uint32_t groupCountX = (minThreads.width + groupSize.width - 1) / groupSize.width;
			uint32_t groupCountY = (minThreads.height + groupSize.height - 1) / groupSize.height;
			uint32_t groupCountZ = (minThreads.depth + groupSize.depth - 1) / groupSize.depth;
			vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);
		}

		void cmdPipelineBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
			VkImageAspectFlags aspectMask)
		{
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = aspectMask;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = srcAccessMask(oldLayout);
			barrier.dstAccessMask = dstAccessMask(newLayout);

			vkCmdPipelineBarrier(cmd,
				srcStage(barrier.srcAccessMask), dstStage(barrier.dstAccessMask),
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
		}

		void cmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
			const std::vector<VkImageMemoryBarrier>& barriers)
		{
			if (barriers.empty())
				return;

			vkCmdPipelineBarrier(commandBuffer,
				srcStage, dstStage,
				0,
				0, nullptr,
				0, nullptr,
				static_cast<uint32_t>(barriers.size()), barriers.data()
			);
		}

		void cmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
			const std::vector<VkBufferMemoryBarrier>& bufferBarriers, const std::vector<VkImageMemoryBarrier>& imageBarriers)
		{
			if (bufferBarriers.empty() && imageBarriers.empty())
				return;

			vkCmdPipelineBarrier(commandBuffer,
				srcStage, dstStage, 0,
				0, nullptr,
				static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
				static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data()
			);
		}

		void cmdScissor(VkCommandBuffer commandBuffer, VkExtent2D extent)
		{
			VkRect2D scissor{};
			scissor.offset = { 0, 0 };
			scissor.extent = extent;
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		}

		void cmdTransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout,
			VkImageLayout newLayout, uint32_t miplevels = 1, uint32_t layoutCount = 1)
		{
			if (oldLayout == newLayout)
				return;

			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = aspectFlags(format);
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = miplevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = layoutCount;
			barrier.srcAccessMask = Tools::srcAccessMask(barrier.oldLayout);
			barrier.dstAccessMask = Tools::dstAccessMask(barrier.newLayout);

			VkPipelineStageFlags srcStage = Tools::srcStage(barrier.srcAccessMask);
			VkPipelineStageFlags dstStage = Tools::dstStage(barrier.dstAccessMask);

			vkCmdPipelineBarrier(cmd,
				srcStage, dstStage,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
		}

		void cmdViewport(VkCommandBuffer commandBuffer, VkExtent2D extent)
		{
			VkViewport viewport{};
			viewport.width = static_cast<float>(extent.width);
			viewport.height = static_cast<float>(extent.height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		}


		// Extensions

		void cmdBeginDebugUtilsLabel(VkCommandBuffer cmd, const char* label, const glm::vec4& color = glm::vec4{ 1.0f })
		{
			if constexpr (Graphics::ENABLE_VALIDATION)
			{
				VkDebugUtilsLabelEXT labelInfo{};
				labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
				labelInfo.pLabelName = label;
				labelInfo.color[0] = color.r;
				labelInfo.color[1] = color.g;
				labelInfo.color[2] = color.b;
				labelInfo.color[3] = color.a;
				vkCmdBeginDebugUtilsLabelEXT(cmd, &labelInfo);
			}
		}

		void cmdEndDebugUtilsLabel(VkCommandBuffer cmd)
		{
			if constexpr (Graphics::ENABLE_VALIDATION)
			{
				vkCmdEndDebugUtilsLabelEXT(cmd);
			}
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
}
