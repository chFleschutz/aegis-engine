module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <memory>
#include <unordered_map>
#include <vector>

export module Aegis.Graphics.Descriptors;

import Aegis.Graphics.Globals;
import Aegis.Graphics.VulkanContext;

export namespace Aegis::Graphics
{
	class DescriptorSetLayout
	{
		friend class DescriptorWriter;

	public:
		struct CreateInfo
		{
			std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings;
			VkDescriptorBindingFlags bindingFlags;
			VkDescriptorSetLayoutCreateFlags flags{ 0 };
		};

		class Builder
		{
		public:
			Builder() = default;

			auto addBinding(uint32_t binding, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags,
				uint32_t count = 1) -> Builder&
			{
				AGX_ASSERT_X(m_createInfo.bindings.count(binding) == 0, "Binding already in use");

				VkDescriptorSetLayoutBinding layoutBinding{
					.binding = binding,
					.descriptorType = descriptorType,
					.descriptorCount = count,
					.stageFlags = stageFlags,
				};
				m_createInfo.bindings[binding] = layoutBinding;
				return *this;
			}

			auto setBindingFlags(VkDescriptorBindingFlags flags) -> Builder&
			{
				m_createInfo.bindingFlags = flags;
				return *this;
			}

			auto setFlags(VkDescriptorSetLayoutCreateFlags flags) -> Builder&
			{
				m_createInfo.flags = flags;
				return *this;
			}

			auto buildUnique() -> std::unique_ptr<DescriptorSetLayout>
			{
				return std::make_unique<DescriptorSetLayout>(m_createInfo);
			}

			auto build() -> DescriptorSetLayout
			{
				return DescriptorSetLayout{ m_createInfo };
			}

		private:
			CreateInfo m_createInfo{};
		};

		DescriptorSetLayout(CreateInfo& createInfo)
			: m_bindings{ std::move(createInfo.bindings) }
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
			setLayoutBindings.reserve(m_bindings.size());
			for (const auto& [location, binding] : m_bindings)
			{
				setLayoutBindings.emplace_back(binding);
			}

			std::vector<VkDescriptorBindingFlags> bindingFlagsVector(setLayoutBindings.size(), createInfo.bindingFlags);
			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
				.bindingCount = static_cast<uint32_t>(bindingFlagsVector.size()),
				.pBindingFlags = bindingFlagsVector.data(),
			};

			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.pNext = (createInfo.bindingFlags != 0) ? &bindingFlagsInfo : nullptr,
				.flags = createInfo.flags,
				.bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
				.pBindings = setLayoutBindings.data(),
			};

			VK_CHECK(vkCreateDescriptorSetLayout(VulkanContext::device(), &descriptorSetLayoutInfo, nullptr, &m_descriptorSetLayout))
		}

		DescriptorSetLayout(const DescriptorSetLayout&) = delete;
		DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
			: m_descriptorSetLayout{ other.m_descriptorSetLayout }, m_bindings{ std::move(other.m_bindings) }
		{
			other.m_descriptorSetLayout = VK_NULL_HANDLE;
		}

		~DescriptorSetLayout()
		{
			vkDestroyDescriptorSetLayout(VulkanContext::device(), m_descriptorSetLayout, nullptr);
		}

		auto operator=(const DescriptorSetLayout&) -> DescriptorSetLayout & = delete;
		auto operator=(DescriptorSetLayout&& other) noexcept -> DescriptorSetLayout&
		{
			if (this != &other)
			{
				m_descriptorSetLayout = other.m_descriptorSetLayout;
				m_bindings = std::move(other.m_bindings);
				other.m_descriptorSetLayout = VK_NULL_HANDLE;
			}
			return *this;
		}

		operator VkDescriptorSetLayout() const { return m_descriptorSetLayout; }
		auto descriptorSetLayout() const -> VkDescriptorSetLayout { return m_descriptorSetLayout; }

		auto allocateDescriptorSet() const -> VkDescriptorSet
		{
			VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
			VulkanContext::descriptorPool().allocateDescriptorSet(VulkanContext::device(), m_descriptorSetLayout, descriptorSet);
			return descriptorSet;
		}

	private:
		VkDescriptorSetLayout m_descriptorSetLayout{ VK_NULL_HANDLE };
		std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> m_bindings;
	};



	class DescriptorWriter
	{
	public:
		DescriptorWriter(DescriptorSetLayout& setLayout)
			: m_setLayout{ setLayout }
		{}

		DescriptorWriter(const DescriptorWriter&) = delete;
		DescriptorWriter(DescriptorWriter&&) = default;
		~DescriptorWriter() = default;

		auto operator=(const DescriptorWriter&) -> DescriptorWriter & = delete;
		auto operator=(DescriptorWriter&&) -> DescriptorWriter & = default;

		auto writeImage(uint32_t binding, VkDescriptorImageInfo textureInfo) -> DescriptorWriter&
		{
			m_imageInfos.emplace_back(binding, std::move(textureInfo));
			return *this;
		}

		auto writeBuffer(uint32_t binding, VkDescriptorBufferInfo bufferInfo) -> DescriptorWriter&
		{
			m_bufferInfos.emplace_back(binding, std::move(bufferInfo));
			return *this;
		}

		void update(VkDescriptorSet set)
		{
			std::vector<VkWriteDescriptorSet> writes;
			writes.reserve(m_bufferInfos.size() + m_imageInfos.size());

			for (const auto& [binding, info] : m_imageInfos)
			{
				AGX_ASSERT_X(m_setLayout.m_bindings.contains(binding), "Binding was not found in Descriptor Set Layout");
				auto& bindingDesc = m_setLayout.m_bindings[binding];
				AGX_ASSERT_X(bindingDesc.descriptorCount == 1, "Cannot write multiple images to a single descriptor");

				VkWriteDescriptorSet write{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = set,
					.dstBinding = binding,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = bindingDesc.descriptorType,
					.pImageInfo = &info,
				};
				writes.emplace_back(write);
			}

			for (const auto& [binding, info] : m_bufferInfos)
			{
				AGX_ASSERT_X(m_setLayout.m_bindings.contains(binding), "Binding was not found in Descriptor Set Layout");
				auto& bindingDesc = m_setLayout.m_bindings[binding];
				AGX_ASSERT_X(bindingDesc.descriptorCount == 1, "Cannot write multiple buffers to a single descriptor");

				VkWriteDescriptorSet write{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = set,
					.dstBinding = binding,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = bindingDesc.descriptorType,
					.pBufferInfo = &info,
				};
				writes.emplace_back(write);
			}

			vkUpdateDescriptorSets(VulkanContext::device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}

	private:
		DescriptorSetLayout& m_setLayout;
		std::vector<std::pair<uint32_t, VkDescriptorImageInfo>> m_imageInfos;
		std::vector<std::pair<uint32_t, VkDescriptorBufferInfo>> m_bufferInfos;
	};



	class DescriptorSet
	{
	public:
		class Builder
		{
		public:
			Builder(DescriptorSetLayout& setLayout)
				: m_setLayout{ setLayout }, m_writer{ setLayout, }
			{}

			Builder(const Builder&) = delete;
			~Builder() = default;

			auto operator=(const Builder&) noexcept -> Builder & = delete;

			auto addBuffer(uint32_t binding, VkDescriptorBufferInfo bufferInfo) -> Builder&
			{
				m_writer.writeBuffer(binding, std::move(bufferInfo));
				return *this;
			}

			auto addTexture(uint32_t binding, VkDescriptorImageInfo textureInfo) -> Builder&
			{
				m_writer.writeImage(binding, std::move(textureInfo));
				return *this;
			}

			auto build() -> DescriptorSet
			{
				auto set = DescriptorSet{ m_setLayout };
				m_writer.update(set.descriptorSet());
				return set;
			}

			auto buildUnique() -> std::unique_ptr<DescriptorSet>
			{
				auto set = std::make_unique<DescriptorSet>(m_setLayout);
				m_writer.update(set->descriptorSet());
				return set;
			}

		private:
			DescriptorSetLayout& m_setLayout;
			DescriptorWriter m_writer;
		};

		DescriptorSet(DescriptorSetLayout& setLayout)
		{
			VulkanContext::descriptorPool().allocateDescriptorSet(VulkanContext::device(), setLayout, m_descriptorSet);
		}

		DescriptorSet(DescriptorSetLayout& setLayout, DescriptorPool& pool)
		{
			pool.allocateDescriptorSet(VulkanContext::device(), setLayout, m_descriptorSet);
		}

		DescriptorSet(const DescriptorSet&) = delete;
		DescriptorSet(DescriptorSet&&) = default;
		~DescriptorSet() = default;

		auto operator=(const DescriptorSet&) -> DescriptorSet & = delete;
		auto operator=(DescriptorSet&&) -> DescriptorSet & = default;

		operator VkDescriptorSet() const { return m_descriptorSet; }
		auto descriptorSet() const -> VkDescriptorSet { return m_descriptorSet; }

		void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
			VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const
		{
			vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);
		}

	private:
		VkDescriptorSet m_descriptorSet{ VK_NULL_HANDLE };
	};
}
