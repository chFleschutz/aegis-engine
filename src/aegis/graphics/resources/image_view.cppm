module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

export module Aegis.Graphics.ImageView;

import Aegis.Graphics.VulkanContext;
import Aegis.Graphics.Image;

export namespace Aegis::Graphics
{
	class Image;

	class ImageView
	{
	public:
		struct CreateInfo
		{
			static constexpr uint32_t USE_IMAGE_MIP_LEVELS = 0;
			static constexpr uint32_t USE_IMAGE_LAYERS = 0;

			uint32_t baseMipLevel = 0;
			uint32_t levelCount = USE_IMAGE_MIP_LEVELS;
			uint32_t baseLayer = 0;
			uint32_t layerCount = USE_IMAGE_LAYERS;
			VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
		};

		ImageView() = default;
		explicit ImageView(const CreateInfo& info, const Image& image)
		{
			AGX_ASSERT_X(info.baseMipLevel + info.levelCount <= image.mipLevels(), "Invalid mip level range");
			AGX_ASSERT_X(info.baseLayer + info.layerCount <= image.layerCount(), "Invalid layer range");

			VkImageViewCreateInfo viewInfo{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = image.image(),
				.viewType = info.viewType,
				.format = image.format(),
				.subresourceRange = VkImageSubresourceRange{
					.aspectMask = VulkanContext::device().findAspectFlags(image.format()),
					.baseMipLevel = info.baseMipLevel,
					.levelCount = info.levelCount == CreateInfo::USE_IMAGE_MIP_LEVELS ? image.mipLevels() : info.levelCount,
					.baseArrayLayer = info.baseLayer,
					.layerCount = info.layerCount == CreateInfo::USE_IMAGE_LAYERS ? image.layerCount() : info.layerCount
				}
			};
			VK_CHECK(vkCreateImageView(VulkanContext::device(), &viewInfo, nullptr, &m_imageView));
		}

		ImageView(const ImageView&) = delete;
		ImageView(ImageView&& other) noexcept
			: m_imageView{ other.m_imageView }
		{
			other.m_imageView = VK_NULL_HANDLE;
		}

		~ImageView()
		{
			destroy();
		}

		auto operator=(const ImageView&) -> ImageView & = delete;
		auto operator=(ImageView&& other) noexcept -> ImageView&
		{
			if (this != &other)
			{
				destroy();
				m_imageView = other.m_imageView;
				other.m_imageView = VK_NULL_HANDLE;
			}
			return *this;
		}

		operator VkImageView() const { return m_imageView; }

		[[nodiscard]] auto imageView() const -> VkImageView { return m_imageView; }

	private:
		void destroy()
		{
			VulkanContext::destroy(m_imageView);
			m_imageView = VK_NULL_HANDLE;
		}

		VkImageView m_imageView = VK_NULL_HANDLE;
	};
}
