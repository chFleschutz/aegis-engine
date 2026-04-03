module;

#include "graphics/vulkan/vulkan_include.h"

export module Aegis.Graphics.Sampler;

import Aegis.Graphics.VulkanContext;

export namespace Aegis::Graphics
{
	class Sampler
	{
	public:
		struct CreateInfo
		{
			static constexpr float USE_MIP_LEVELS = -1.0f;

			VkFilter magFilter = VK_FILTER_LINEAR;
			VkFilter minFilter = VK_FILTER_LINEAR;
			VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			float maxLod = USE_MIP_LEVELS;
			bool anisotropy = true;
		};

		Sampler() = default;
		explicit Sampler(const CreateInfo& config, uint32_t mipLevels = 1)
		{
			VkSamplerCreateInfo samplerInfo{
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.magFilter = config.magFilter,
				.minFilter = config.minFilter,
				.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
				.addressModeU = config.addressMode,
				.addressModeV = config.addressMode,
				.addressModeW = config.addressMode,
				.mipLodBias = 0.0f,
				.anisotropyEnable = config.anisotropy,
				.maxAnisotropy = VulkanContext::device().properties().limits.maxSamplerAnisotropy,
				.compareEnable = VK_FALSE,
				.compareOp = VK_COMPARE_OP_ALWAYS,
				.minLod = 0.0f,
				.maxLod = config.maxLod < 0.0f ? static_cast<float>(mipLevels - 1) : config.maxLod,
				.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
				.unnormalizedCoordinates = VK_FALSE,
			};
			VK_CHECK(vkCreateSampler(VulkanContext::device(), &samplerInfo, nullptr, &m_sampler));
		}

		Sampler(const Sampler&) = delete;
		Sampler(Sampler&& other) noexcept
			: m_sampler{ other.m_sampler }
		{
			other.m_sampler = VK_NULL_HANDLE;
		}

		~Sampler()
		{
			destroy();
		}

		auto operator=(const Sampler&)->Sampler & = delete;
		auto operator=(Sampler&& other) noexcept -> Sampler&
		{
			if (this != &other)
			{
				destroy();
				m_sampler = other.m_sampler;
				other.m_sampler = VK_NULL_HANDLE;
			}
			return *this;
		}

		operator VkSampler() const { return m_sampler; }

		[[nodiscard]] auto sampler() const -> VkSampler { return m_sampler; }

	private:
		void destroy()
		{
			VulkanContext::destroy(m_sampler);
			m_sampler = VK_NULL_HANDLE;
		}

		VkSampler m_sampler = VK_NULL_HANDLE;
	};
}
