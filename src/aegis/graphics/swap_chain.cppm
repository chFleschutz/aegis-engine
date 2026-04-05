module;

#include "graphics/vulkan/vulkan_include.h"

#include <aegis-log/log.h>

#include <vector>

export module Aegis.Graphics.SwapChain;

import Aegis.Graphics.VulkanContext;

export namespace Aegis::Graphics
{
	class SwapChain
	{
	public:
		struct ImageSync
		{
			VkFence inFlightFence;
			VkSemaphore presentReady;
		};

		SwapChain(VkExtent2D windowExtent)
			: m_windowExtent{ windowExtent }
		{
			createSwapChain();
			createImageViews();

			m_imageSync.resize(m_images.size());
			for (auto& sync : m_imageSync)
			{
				sync.inFlightFence = VK_NULL_HANDLE;

				VkSemaphoreCreateInfo semaphoreInfo{
					.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				};

				VK_CHECK(vkCreateSemaphore(VulkanContext::device(), &semaphoreInfo, nullptr, &sync.presentReady));
			}
		}

		SwapChain(const SwapChain&) = delete;
		~SwapChain()
		{
			destroyImageViews();
			destroySwapChain(m_swapChain);

			for (auto& sync : m_imageSync)
			{
				vkDestroySemaphore(VulkanContext::device(), sync.presentReady, nullptr);
			}
		}

		void operator=(const SwapChain&) = delete;

		[[nodiscard]] auto extent() const -> VkExtent2D { return m_extent; }
		[[nodiscard]] auto width() const -> uint32_t { return m_extent.width; }
		[[nodiscard]] auto height() const -> uint32_t { return m_extent.height; }
		[[nodiscard]] auto aspectRatio() const -> float { return static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height); }
		[[nodiscard]] auto imageCount() const -> size_t { return m_images.size(); }
		[[nodiscard]] auto currentImage() const -> VkImage { return m_images[m_imageIndex]; }
		[[nodiscard]] auto presentReadySemaphore() const -> VkSemaphore { return m_imageSync[m_imageIndex].presentReady; }
		[[nodiscard]] auto findDepthFormat() -> VkFormat
		{
			// TODO: Rendersystems assume VK_FORMAT_D32_SFLOAT as the depth format
			return VulkanContext::device().findSupportedFormat(
				{ VK_FORMAT_D32_SFLOAT/*, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT*/ },
				VK_IMAGE_TILING_OPTIMAL,
				VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
		}

		auto acquireNextImage(VkSemaphore imageAvailable) -> VkResult
		{
			return vkAcquireNextImageKHR(VulkanContext::device(), m_swapChain, std::numeric_limits<uint64_t>::max(),
				imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
		}

		void waitForImageInFlight(VkFence frameFence)
		{
			if (m_imageSync[m_imageIndex].inFlightFence != VK_NULL_HANDLE)
			{
				vkWaitForFences(VulkanContext::device(), 1, &m_imageSync[m_imageIndex].inFlightFence, VK_TRUE,
					std::numeric_limits<uint64_t>::max());
			}
			m_imageSync[m_imageIndex].inFlightFence = frameFence;
		}

		auto present() -> VkResult
		{
			VkPresentInfoKHR presentInfo{
				.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &m_imageSync[m_imageIndex].presentReady,
				.swapchainCount = 1,
				.pSwapchains = &m_swapChain,
				.pImageIndices = &m_imageIndex,
			};

			return vkQueuePresentKHR(VulkanContext::device().presentQueue(), &presentInfo);
		}

		auto compareSwapFormats(const SwapChain& swapchain) const -> bool { return swapchain.m_format == m_format; }

		void resize(VkExtent2D extent)
		{
			destroyImageViews();

			m_windowExtent = extent;

			createSwapChain();
			createImageViews();

			ALOG::info("Swapchain resized to {}x{}", extent.width, extent.height);
		}

	private:
		void createSwapChain()
		{
			SwapChainSupportDetails swapChainSupport = VulkanContext::device().querySwapChainSupport();
			VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
			VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
			VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
			uint32_t imageCount = chooseImageCount(swapChainSupport.capabilities);

			VkSwapchainCreateInfoKHR createInfo{
				.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
				.surface = VulkanContext::device().surface(),
				.minImageCount = imageCount,
				.imageFormat = surfaceFormat.format,
				.imageColorSpace = surfaceFormat.colorSpace,
				.imageExtent = extent,
				.imageArrayLayers = 1,
				.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				.preTransform = swapChainSupport.capabilities.currentTransform,
				.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				.presentMode = presentMode,
				.clipped = VK_TRUE,
				.oldSwapchain = m_swapChain,
			};

			QueueFamilyIndices indices = VulkanContext::device().findPhysicalQueueFamilies();
			AGX_ASSERT_X(indices.isComplete(), "Queue family indices are not complete");

			std::array<uint32_t, 2> queueFamilyIndices{ indices.graphicsFamily.value(), indices.presentFamily.value() };
			if (indices.graphicsFamily != indices.presentFamily)
			{
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = queueFamilyIndices.size();
				createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
			}
			else
			{
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				createInfo.queueFamilyIndexCount = 0;
				createInfo.pQueueFamilyIndices = nullptr;
			}

			VK_CHECK(vkCreateSwapchainKHR(VulkanContext::device(), &createInfo, nullptr, &m_swapChain));
			destroySwapChain(createInfo.oldSwapchain);

			vkGetSwapchainImagesKHR(VulkanContext::device(), m_swapChain, &imageCount, nullptr);
			m_images.resize(imageCount);
			vkGetSwapchainImagesKHR(VulkanContext::device(), m_swapChain, &imageCount, m_images.data());

			m_format = surfaceFormat.format;
			m_extent = extent;

			// At the beginning of a frame all swapchain images should be in present layout
			for (const auto& image : m_images)
			{
				VulkanContext::device().transitionImageLayout(image, m_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
			}
		}

		void createImageViews()
		{
			m_imageViews.resize(m_images.size());
			for (size_t i = 0; i < m_images.size(); i++)
			{
				VkImageViewCreateInfo viewInfo{};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = m_images[i];
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format = m_format;
				viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewInfo.subresourceRange.baseMipLevel = 0;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.baseArrayLayer = 0;
				viewInfo.subresourceRange.layerCount = 1;

				VK_CHECK(vkCreateImageView(VulkanContext::device().device(), &viewInfo, nullptr, &m_imageViews[i]));
			}
		}

		void destroyImageViews()
		{
			for (auto imageView : m_imageViews)
			{
				vkDestroyImageView(VulkanContext::device(), imageView, nullptr);
			}
			m_imageViews.clear();
		}

		void destroySwapChain(VkSwapchainKHR swapChain)
		{
			if (swapChain)
			{
				vkDestroySwapchainKHR(VulkanContext::device(), swapChain, nullptr);
			}
		}

		auto chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const -> VkSurfaceFormatKHR
		{
			for (const auto& availableFormat : availableFormats)
			{
				if (availableFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
					continue;

				// Gamma correction is already handled during post-processing -> use linear color format
				if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM || availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM)
				{
					return availableFormat;
				}
			}

			AGX_ASSERT_X(false, "No suitable surface format found");
			return availableFormats[0];
		}

		auto chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const -> VkPresentModeKHR
		{
			for (const auto& availablePresentMode : availablePresentModes)
			{
				if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
					return availablePresentMode;
			}

			return VK_PRESENT_MODE_FIFO_KHR;
		}

		auto chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const -> VkExtent2D
		{
			if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
			{
				return capabilities.currentExtent;
			}
			else
			{
				VkExtent2D actualExtent = m_windowExtent;
				actualExtent.width = std::max(
					capabilities.minImageExtent.width,
					std::min(capabilities.maxImageExtent.width, actualExtent.width));
				actualExtent.height = std::max(
					capabilities.minImageExtent.height,
					std::min(capabilities.maxImageExtent.height, actualExtent.height));

				return actualExtent;
			}
		}

		auto chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities) const -> uint32_t
		{
			auto imageCount = capabilities.minImageCount + 1;
			if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
			{
				imageCount = capabilities.maxImageCount;
			}

			return imageCount;
		}

		VkFormat m_format;
		VkExtent2D m_extent;
		VkExtent2D m_windowExtent;
		VkSwapchainKHR m_swapChain{ VK_NULL_HANDLE };
		std::vector<VkImage> m_images;
		std::vector<VkImageView> m_imageViews;
		std::vector<ImageSync> m_imageSync;
		uint32_t m_imageIndex{ 0 };
	};
}
