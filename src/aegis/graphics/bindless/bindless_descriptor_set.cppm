module;

#include "core/assert.h"
#include "graphics/vulkan/volk_include.h"

#include <cstdint>
#include <vector>

export module Aegis.Graphics.Bindless:BindlessDescriptorSet;

import :DescriptorHandle;
import Aegis.Graphics.VulkanContext;
import Aegis.Graphics.Descriptors;

namespace Aegis::Graphics::Bindless
{
	class DescriptorHandleCache
	{
	public:
		DescriptorHandleCache(uint32_t capacity) :
			m_capacity{ capacity }
		{
		}
		
		auto fetch(DescriptorHandle::Type type) -> DescriptorHandle
		{
			if (!m_availableHandles.empty())
			{
				DescriptorHandle handle = m_availableHandles.back();
				m_availableHandles.pop_back();
				handle.recycle(type);
				return handle;
			}

			AGX_ASSERT_X(m_nextIndex < m_capacity, "DescriptorHandleCache capacity exceeded!");
			return DescriptorHandle{ m_nextIndex++, type };
		}

		void free(DescriptorHandle& handle)
		{
			if (!handle.isValid())
				return;

			AGX_ASSERT_X(handle.index() < m_capacity, "DescriptorHandle index out of bounds!");
			m_availableHandles.emplace_back(handle);
			handle.invalidate();
		}

	private:
		uint32_t m_capacity;
		uint32_t m_nextIndex{ 0 };
		std::vector<DescriptorHandle> m_availableHandles;
	};



	class BindlessDescriptorSet
	{
	public:
		static constexpr uint32_t MAX_SAMPLED_IMAGES = 1 << 18;
		static constexpr uint32_t MAX_STORAGE_IMAGES = 1 << 14;
		static constexpr uint32_t MAX_STORAGE_BUFFERS = 1 << 19;
		static constexpr uint32_t MAX_UNIFORM_BUFFERS = 1 << 16;

		static constexpr uint32_t SAMPLED_IMAGE_BINDING = 0;
		static constexpr uint32_t STORAGE_IMAGE_BINDING = 1;
		static constexpr uint32_t STORAGE_BUFFER_BINDING = 2;
		static constexpr uint32_t UNIFORM_BUFFER_BINDING = 3;

		BindlessDescriptorSet() :
			m_bindlessPool{ createDescriptorPool() },
			m_bindlessSetLayout{ createDescriptorSetLayout() },
			m_bindlessDescriptorSet{ m_bindlessSetLayout, m_bindlessPool }
		{}

		~BindlessDescriptorSet() = default;

		operator VkDescriptorSet() const { return m_bindlessDescriptorSet.descriptorSet(); }

		[[nodiscard]] auto descriptorSet() const -> const DescriptorSet& { return m_bindlessDescriptorSet; }
		[[nodiscard]] auto layout() const -> const DescriptorSetLayout& { return m_bindlessSetLayout; }

		auto allocateSampledImage(const Texture& texture) -> DescriptorHandle
		{
			auto handle = m_sampledImageCache.fetch(DescriptorHandle::Type::SampledImage);
			auto textureInfo = texture.descriptorImageInfo(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			writeSet(SAMPLED_IMAGE_BINDING, handle.index(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &textureInfo, nullptr);
			return handle;
		}

		auto allocateStorageImage(const Texture& texture) -> DescriptorHandle
		{
			auto handle = m_storageImageCache.fetch(DescriptorHandle::Type::StorageImage);
			auto textureInfo = texture.descriptorImageInfo(VK_IMAGE_LAYOUT_GENERAL);
			writeSet(STORAGE_IMAGE_BINDING, handle.index(), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &textureInfo, nullptr);
			return handle;
		}

		auto allocateStorageBuffer(const VkDescriptorBufferInfo& bufferInfo) -> DescriptorHandle
		{
			auto handle = m_storageBufferCache.fetch(DescriptorHandle::Type::StorageBuffer);
			writeSet(STORAGE_BUFFER_BINDING, handle.index(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo);
			return handle;
		}

		auto allocateUniformBuffer(const VkDescriptorBufferInfo& bufferInfo) -> DescriptorHandle
		{
			auto handle = m_uniformBufferCache.fetch(DescriptorHandle::Type::UniformBuffer);
			writeSet(UNIFORM_BUFFER_BINDING, handle.index(), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo);
			return handle;
		}

		void freeHandle(DescriptorHandle& handle)
		{
			if (!handle.isValid())
				return;

			switch (handle.type())
			{
			case DescriptorHandle::Type::SampledImage:
				m_sampledImageCache.free(handle);
				break;
			case DescriptorHandle::Type::StorageImage:
				m_storageImageCache.free(handle);
				break;
			case DescriptorHandle::Type::StorageBuffer:
				m_storageBufferCache.free(handle);
				break;
			case DescriptorHandle::Type::UniformBuffer:
				m_uniformBufferCache.free(handle);
				break;
			default:
				AGX_ASSERT_X(false, "Unknown DescriptorHandle type!");
				break;
			}
		}

	private:
		auto createDescriptorPool() -> DescriptorPool
		{
			const auto& limits = VulkanContext::device().properties().limits;
			return DescriptorPool::Builder()
				.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, std::min(MAX_SAMPLED_IMAGES, limits.maxDescriptorSetSampledImages))
				.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, std::min(MAX_STORAGE_IMAGES, limits.maxDescriptorSetStorageImages))
				.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, std::min(MAX_STORAGE_BUFFERS, limits.maxDescriptorSetStorageBuffers))
				.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, std::min(MAX_UNIFORM_BUFFERS, limits.maxDescriptorSetUniformBuffers))
				.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
				.setMaxSets(1)
				.build();
		}

		auto createDescriptorSetLayout() -> DescriptorSetLayout
		{
			const auto& limits = VulkanContext::device().properties().limits;
			return DescriptorSetLayout::Builder()
				.addBinding(SAMPLED_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL,
					std::min(MAX_SAMPLED_IMAGES, limits.maxDescriptorSetSampledImages))
				.addBinding(STORAGE_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_ALL,
					std::min(MAX_STORAGE_IMAGES, limits.maxDescriptorSetStorageImages))
				.addBinding(STORAGE_BUFFER_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL,
					std::min(MAX_STORAGE_BUFFERS, limits.maxDescriptorSetStorageBuffers))
				.addBinding(UNIFORM_BUFFER_BINDING, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL,
					std::min(MAX_UNIFORM_BUFFERS, limits.maxDescriptorSetUniformBuffers))
				.setBindingFlags(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
				.setFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
				.build();
		}

		void writeSet(uint32_t binding, uint32_t index, VkDescriptorType type,
			const VkDescriptorImageInfo* imageInfo, const VkDescriptorBufferInfo* bufferInfo)
		{
			VkWriteDescriptorSet write{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = m_bindlessDescriptorSet,
				.dstBinding = binding,
				.dstArrayElement = index,
				.descriptorCount = 1,
				.descriptorType = type,
				.pImageInfo = imageInfo,
				.pBufferInfo = bufferInfo,
			};
			vkUpdateDescriptorSets(VulkanContext::device(), 1, &write, 0, nullptr);
		}

		DescriptorPool m_bindlessPool;
		DescriptorSetLayout m_bindlessSetLayout;
		DescriptorSet m_bindlessDescriptorSet;

		DescriptorHandleCache m_sampledImageCache{ MAX_SAMPLED_IMAGES };
		DescriptorHandleCache m_storageImageCache{ MAX_STORAGE_IMAGES };
		DescriptorHandleCache m_storageBufferCache{ MAX_STORAGE_BUFFERS };
		DescriptorHandleCache m_uniformBufferCache{ MAX_UNIFORM_BUFFERS };
	};
}