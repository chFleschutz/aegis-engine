module;

#include "graphics/vulkan/vulkan_include.h"

#include <cstdint>
#include <vector>
#include <memory>

export module Aegis.Graphics.DescriptorPool;

export namespace Aegis::Graphics
{
	class DescriptorPool
	{
	public:
		class Builder
		{
		public:
			Builder() = default;

			auto addPoolSize(VkDescriptorType descriptorType, uint32_t count) -> Builder&
			{
				m_poolSizes.push_back({ descriptorType, count });
				return *this;
			}

			auto setPoolFlags(VkDescriptorPoolCreateFlags flags) -> Builder&
			{
				m_poolFlags = flags;
				return *this;
			}

			auto setMaxSets(uint32_t count) -> Builder&
			{
				m_maxSets = count;
				return *this;
			}

			auto build(VkDevice device) const -> DescriptorPool
			{
				return DescriptorPool{ device, m_maxSets, m_poolFlags, m_poolSizes };
			}

		private:
			std::vector<VkDescriptorPoolSize> m_poolSizes{};
			uint32_t m_maxSets = 1000;
			VkDescriptorPoolCreateFlags m_poolFlags = 0;
		};

		DescriptorPool() = default;
		DescriptorPool(VkDevice device, uint32_t maxSets, VkDescriptorPoolCreateFlags poolFlags,
			const std::vector<VkDescriptorPoolSize>& poolSizes)
		{
			VkDescriptorPoolCreateInfo descriptorPoolInfo{};
			descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			descriptorPoolInfo.pPoolSizes = poolSizes.data();
			descriptorPoolInfo.maxSets = maxSets;
			descriptorPoolInfo.flags = poolFlags;

			VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &m_descriptorPool))
		}

		DescriptorPool(const DescriptorPool&) = delete;
		DescriptorPool(DescriptorPool&& other) noexcept
			: m_descriptorPool{ other.m_descriptorPool }
		{
			other.m_descriptorPool = VK_NULL_HANDLE;
		}

		void destroy(VkDevice device)
		{
			if (m_descriptorPool)
			{
				vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
			}
		}

		auto operator=(const DescriptorPool&) -> DescriptorPool & = delete;
		auto operator=(DescriptorPool&& other) noexcept -> DescriptorPool&
		{
			if (this != &other)
			{
				m_descriptorPool = other.m_descriptorPool;
				other.m_descriptorPool = VK_NULL_HANDLE;
			}
			return *this;
		}

		operator VkDescriptorPool() const { return m_descriptorPool; }
		auto descriptorPool() const -> VkDescriptorPool { return m_descriptorPool; }

		void allocateDescriptorSet(VkDevice device, const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptorSet) const
		{
			VkDescriptorSetAllocateInfo allocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = m_descriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &descriptorSetLayout,
			};

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet))
		}

		void freeDescriptors(VkDevice device, std::vector<VkDescriptorSet>& descriptors) const
		{
			vkFreeDescriptorSets(device, m_descriptorPool, static_cast<uint32_t>(descriptors.size()), descriptors.data());
		}

		void resetPool(VkDevice device)
		{
			vkResetDescriptorPool(device, m_descriptorPool, 0);
		}

	private:
		VkDescriptorPool m_descriptorPool{ VK_NULL_HANDLE };
	};
}
