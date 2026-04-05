module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

export module Aegis.Graphics.VulkanContext;

import Aegis.Core.Window;
import Aegis.Graphics.Vulkan.Device;
import Aegis.Graphics.DeletionQueue;
import Aegis.Graphics.DescriptorPool;

export namespace Aegis::Graphics
{
	class VulkanContext
	{
	public:
		~VulkanContext() = default;
		VulkanContext(const VulkanContext&) = delete;
		VulkanContext(VulkanContext&&) = delete;

		auto operator=(const VulkanContext&) -> VulkanContext & = delete;
		auto operator=(VulkanContext&&) -> VulkanContext & = delete;

		[[nodiscard]] static auto instance() -> VulkanContext&
		{
			static VulkanContext instance;
			return instance;
		}

		[[nodiscard]] static auto device() -> VulkanDevice& { return instance().m_device; }
		[[nodiscard]] static auto descriptorPool() -> DescriptorPool& { return instance().m_descriptorPool; }
		[[nodiscard]] static auto deletionQueue() -> DeletionQueue& { return instance().m_deletionQueue; }

		static auto initialize(Core::Window& window) -> VulkanContext&
		{
			auto& context = instance();
			context.m_device.initialize(window);

			// TODO: Let the pool grow dynamically (see: https://vkguide.dev/docs/extra-chapter/abstracting_descriptors/)
			context.m_descriptorPool = DescriptorPool::Builder{}
				.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
				.setMaxSets(1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, 500)
				.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000)
				.addPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 500)
				.build(VulkanContext::device());

			return context;
		}

		static void destroy()
		{
			auto& context = instance();
			context.m_deletionQueue.flushAll();
			context.m_descriptorPool.destroy(context.m_device);
		}

		static void destroy(VkBuffer buffer, VmaAllocation allocation)
		{
			if (buffer)
			{
				AGX_ASSERT_X(allocation, "Buffer and allocation must be valid");
				VulkanContext::instance().m_deletionQueue.schedule([=]()
					{
						vmaDestroyBuffer(VulkanContext::instance().m_device.allocator(), buffer, allocation);
					});
			}
		}

		static void destroy(VkImage image, VmaAllocation allocation)
		{
			if (image)
			{
				AGX_ASSERT_X(allocation, "Image and allocation must be valid");
				VulkanContext::instance().m_deletionQueue.schedule([=]()
					{
						vmaDestroyImage(VulkanContext::instance().m_device.allocator(), image, allocation);
					});
			}
		}

		static void destroy(VkImageView view)
		{
			if (view)
			{
				VulkanContext::instance().m_deletionQueue.schedule([=]()
					{
						vkDestroyImageView(VulkanContext::instance().m_device.device(), view, nullptr);
					});
			}
		}

		static void destroy(VkSampler sampler)
		{
			if (sampler)
			{
				VulkanContext::instance().m_deletionQueue.schedule([=]()
					{
						vkDestroySampler(VulkanContext::instance().m_device.device(), sampler, nullptr);
					});
			}
		}

		static void destroy(VkPipeline pipeline)
		{
			if (pipeline)
			{
				VulkanContext::instance().m_deletionQueue.schedule([=]()
					{
						vkDestroyPipeline(VulkanContext::instance().m_device.device(), pipeline, nullptr);
					});
			}
		}

		static void destroy(VkPipelineLayout pipelineLayout)
		{
			if (pipelineLayout)
			{
				VulkanContext::instance().m_deletionQueue.schedule([=]()
					{
						vkDestroyPipelineLayout(VulkanContext::instance().m_device.device(), pipelineLayout, nullptr);
					});
			}
		}

		static void flushDeletionQueue(uint32_t frameIndex)
		{
			VulkanContext::instance().m_deletionQueue.flush(frameIndex);
		}

	private:
		VulkanContext() = default;

		// Device
		// Surface
		// Command Pool
		// Vma Allocator
		// Deletion Queue

		VulkanDevice m_device{};
		DescriptorPool m_descriptorPool{};
		DeletionQueue m_deletionQueue{};
	};
}