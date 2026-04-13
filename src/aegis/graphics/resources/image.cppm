module;

#include "graphics/vulkan/vulkan_include.h"

#include <cstdint>

export module Aegis.Graphics.Image;

import Aegis.Graphics.Buffer;
import Aegis.Graphics.VulkanContext;
import Aegis.Graphics.Vulkan.Tools;
import Aegis.Graphics.Vulkan.VulkanMemory;
//import Aegis.Graphics.Vulkan.ResourceTools;

export namespace Aegis::Graphics
{
	class Image
	{
	public:
		struct CreateInfo
		{
			static constexpr uint32_t CALCULATE_MIP_LEVELS = 0;

			VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
			VkExtent3D extent = { 1, 1, 1 };
			uint32_t mipLevels = CALCULATE_MIP_LEVELS;
			uint32_t layerCount = 1;
			VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			VkImageType imageType = VK_IMAGE_TYPE_2D;
			VkImageCreateFlags flags = 0;
		};

		Image() = default;
		explicit Image(const CreateInfo& info) :
			m_extent{ info.extent },
			m_format{ info.format },
			m_mipLevels{ info.mipLevels },
			m_layerCount{ info.layerCount }
		{
			create(info);
		}

		Image(const Image&) = delete;
		Image(Image&& other) noexcept
		{
			m_image = other.m_image;
			m_allocation = other.m_allocation;
			m_format = other.m_format;
			m_extent = other.m_extent;
			m_mipLevels = other.m_mipLevels;
			m_layerCount = other.m_layerCount;
			m_layout = other.m_layout;

			other.m_image = VK_NULL_HANDLE;
			other.m_allocation = VK_NULL_HANDLE;
		}

		~Image()
		{
			destroy();
		}

		auto operator=(const Image&) -> Image & = delete;
		auto operator=(Image&& other) noexcept -> Image&
		{
			if (this != &other)
			{
				destroy();

				m_image = other.m_image;
				m_allocation = other.m_allocation;
				m_format = other.m_format;
				m_extent = other.m_extent;
				m_mipLevels = other.m_mipLevels;
				m_layerCount = other.m_layerCount;
				m_layout = other.m_layout;
				other.m_image = VK_NULL_HANDLE;
				other.m_allocation = VK_NULL_HANDLE;
			}
			return *this;
		}

		operator VkImage() const { return m_image; }

		[[nodiscard]] auto image() const -> VkImage { return m_image; }
		[[nodiscard]] auto format() const -> VkFormat { return m_format; }
		[[nodiscard]] auto extent() const -> VkExtent3D { return m_extent; }
		[[nodiscard]] auto width() const -> uint32_t { return m_extent.width; }
		[[nodiscard]] auto height() const -> uint32_t { return m_extent.height; }
		[[nodiscard]] auto depth() const -> uint32_t { return m_extent.depth; }
		[[nodiscard]] auto mipLevels() const -> uint32_t { return m_mipLevels; }
		[[nodiscard]] auto layerCount() const -> uint32_t { return m_layerCount; }
		[[nodiscard]] auto layout() const -> VkImageLayout { return m_layout; }

		void upload(const Buffer& buffer)
		{
			VkCommandBuffer cmd = VulkanContext::device().beginSingleTimeCommands();
			{
				Tools::vk::cmdTransitionImageLayout(cmd, m_image, m_format, m_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mipLevels, m_layerCount);
				Tools::vk::cmdCopyBufferToImage(cmd, buffer, m_image, m_extent, m_layerCount);
				generateMipmaps(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			VulkanContext::device().endSingleTimeCommands(cmd);
		}

		void upload(const void* data, VkDeviceSize size)
		{
			Buffer stagingBuffer{ Buffer::stagingBuffer(size) };
			stagingBuffer.singleWrite(data);
			upload(stagingBuffer);
		}

		//void fill(const Buffer& buffer);
		//void fill(const void* data, VkDeviceSize size);
		//void fillSFLOAT(const glm::vec4& color);
		//void fillRGBA8(const glm::vec4& color);

		void copyFrom(VkCommandBuffer cmd, const Buffer& src)
		{
			Tools::vk::cmdTransitionImageLayout(cmd, m_image, m_format, m_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mipLevels, m_layerCount);
			Tools::vk::cmdCopyBufferToImage(cmd, src.buffer(), m_image, m_extent, m_layerCount);
			Tools::vk::cmdTransitionImageLayout(cmd, m_image, m_format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_mipLevels, m_layerCount);
		}

		void transitionLayout(VkImageLayout newLayout)
		{
			if (m_layout == newLayout)
				return;
			VkCommandBuffer cmd = VulkanContext::device().beginSingleTimeCommands();
			transitionLayout(cmd, newLayout);
			VulkanContext::device().endSingleTimeCommands(cmd);
		}

		void transitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout)
		{
			if (m_layout == newLayout)
				return;

			Tools::vk::cmdTransitionImageLayout(cmd, m_image, m_format, m_layout, newLayout, m_mipLevels, m_layerCount);
			m_layout = newLayout;
		}

		auto transitionLayoutDeferred(VkImageLayout newLayout) -> VkImageMemoryBarrier
		{
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = m_layout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = m_image;
			barrier.subresourceRange.aspectMask = Tools::aspectFlags(m_format);
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = m_mipLevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = m_layerCount;
			barrier.srcAccessMask = Tools::srcAccessMask(barrier.oldLayout);
			barrier.dstAccessMask = Tools::dstAccessMask(barrier.newLayout);

			m_layout = newLayout;
			return barrier;
		}

		/// @brief Sets the current image layout WITHOUT any doing any transitions
		void setLayout(VkImageLayout layout) { m_layout = layout; }

		void generateMipmaps(VkCommandBuffer cmd, VkImageLayout finalLayout)
		{
			transitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = m_image;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = m_layerCount;
			barrier.subresourceRange.levelCount = 1;

			for (uint32_t i = 1; i < m_mipLevels; i++)
			{
				barrier.subresourceRange.baseMipLevel = i - 1;
				barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
					0, nullptr,
					0, nullptr,
					1, &barrier
				);

				VkImageBlit blit{};
				blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blit.srcSubresource.mipLevel = i - 1;
				blit.srcSubresource.baseArrayLayer = 0;
				blit.srcSubresource.layerCount = m_layerCount;
				blit.srcOffsets[0] = { 0, 0, 0 };
				blit.srcOffsets[1].x = static_cast<int32_t>(m_extent.width >> (i - 1));
				blit.srcOffsets[1].y = static_cast<int32_t>(m_extent.height >> (i - 1));
				blit.srcOffsets[1].z = 1;
				blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blit.dstSubresource.mipLevel = i;
				blit.srcSubresource.baseArrayLayer = 0;
				blit.dstSubresource.layerCount = m_layerCount;
				blit.dstOffsets[0] = { 0, 0, 0 };
				blit.dstOffsets[1].x = static_cast<int32_t>(m_extent.width >> i);
				blit.dstOffsets[1].y = static_cast<int32_t>(m_extent.height >> i);
				blit.dstOffsets[1].z = 1;

				vkCmdBlitImage(cmd,
					m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &blit,
					VK_FILTER_LINEAR);

				barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barrier.newLayout = finalLayout;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barrier.dstAccessMask = Tools::dstAccessMask(finalLayout);

				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_TRANSFER_BIT, Tools::dstStage(barrier.dstAccessMask), 0,
					0, nullptr,
					0, nullptr,
					1, &barrier);
			}

			barrier.subresourceRange.baseMipLevel = m_mipLevels - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = finalLayout;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = Tools::dstAccessMask(finalLayout);

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, Tools::dstStage(barrier.dstAccessMask), 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			m_layout = finalLayout;
		}

	private:
		//void create(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, uint32_t mipLevels = 1, uint32_t layerCount = 1);
		//void create(const std::filesystem::path& path, VkFormat format);
		void create(const CreateInfo& config)
		{
			if (config.mipLevels == CreateInfo::CALCULATE_MIP_LEVELS)
			{
				uint32_t maxDim = std::max(config.extent.width, std::max(config.extent.height, config.extent.depth));
				m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;
			}

			VkImageCreateInfo imageInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.flags = config.flags,
				.imageType = config.imageType,
				.format = m_format,
				.extent = m_extent,
				.mipLevels = m_mipLevels,
				.arrayLayers = m_layerCount,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = config.usage,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			};

			if (m_mipLevels > 1)
			{
				imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}

			vma::AllocationCreateInfo allocInfo{
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			VulkanContext::device().createImage(m_image, m_allocation, imageInfo, allocInfo);
		}

		void destroy()
		{
			VulkanContext::destroy(m_image, m_allocation);
			m_image = VK_NULL_HANDLE;
			m_allocation = VK_NULL_HANDLE;
		}

		VkImage m_image = VK_NULL_HANDLE;
		vma::Allocation m_allocation = VK_NULL_HANDLE;
		VkExtent3D m_extent = { 1, 1, 1 };
		VkFormat m_format = VK_FORMAT_UNDEFINED;
		uint32_t m_mipLevels = 1;
		uint32_t m_layerCount = 1;
		VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	};
}
