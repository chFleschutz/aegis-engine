module;

#include "graphics/descriptors.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/pipeline.h"
#include "graphics/resources/image_view.h"
#include "graphics/resources/sampler.h"
#include "graphics/vulkan/vulkan_tools.h"

export module Aegis.Graphics.RenderPasses.BloomPass;

export namespace Aegis::Graphics
{
	struct BloomThreshold
	{
		float threshold = 0.0f;
		float softThreshold = 0.5f;
	};

	struct BloomDownsample
	{
		int mipLevel = 0;
		float filterScale = 1.0f;
	};

	struct BloomUpsample
	{
		float filterScale = 1.0f;
	};

	/// @brief Bloom post-processing effect using a threshold, downsample, and upsample pass
	/// @note Based on: https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
	class BloomPass : public FGRenderPass
	{
	public:
		static constexpr uint32_t BLOOM_MIP_LEVELS = 6;

		BloomPass(FGResourcePool& pool) :
			m_thresholdSetLayout{ createThresholdDescriptorSetLayout() },
			m_downsampleSetLayout{ createDownsampleDescriptorSetLayout() },
			m_upsampleSetLayout{ createUpsampleDescriptorSetLayout() },
			m_thresholdSet{ m_thresholdSetLayout }
		{
			auto samplerInfo = Sampler::CreateInfo{
				.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.anisotropy = false,
			};
			m_sampler = Sampler{ samplerInfo };

			// Descriptor sets
			m_thresholdPipeline = Pipeline::ComputeBuilder{}
				.addDescriptorSetLayout(m_thresholdSetLayout)
				.addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(BloomThreshold))
				.setShaderStage(SHADER_DIR "bloom/bloom_threshold.slang.spv")
				.buildUnique();

			// Downsample
			for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; i++)
			{
				m_downsampleSets.emplace_back(m_downsampleSetLayout);
			}

			m_downsamplePipeline = Pipeline::ComputeBuilder{}
				.addDescriptorSetLayout(m_downsampleSetLayout)
				.addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(BloomDownsample))
				.setShaderStage(SHADER_DIR "bloom/bloom_downsample.slang.spv")
				.buildUnique();

			// Upsample

			for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; i++)
			{
				m_upsampleSets.emplace_back(m_upsampleSetLayout);
			}

			m_upsamplePipeline = Pipeline::ComputeBuilder{}
				.addDescriptorSetLayout(m_upsampleSetLayout)
				.addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(BloomUpsample))
				.setShaderStage(SHADER_DIR "bloom/bloom_upsample.slang.spv")
				.buildUnique();

			m_sceneColor = pool.addReference("SceneColor",
				FGResource::Usage::ComputeReadStorage);

			m_bloom = pool.addImage("Bloom",
				FGResource::Usage::ComputeWriteStorage,
				FGTextureInfo{
					.format = VK_FORMAT_R16G16B16A16_SFLOAT,
					.resizeMode = FGResizeMode::SwapChainRelative,
					.mipLevels = BLOOM_MIP_LEVELS,
					.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				});
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Bloom",
				.reads = { m_sceneColor },
				.writes = { m_bloom },
			};
		}

		virtual void createResources(FGResourcePool& resources) override
		{
			auto& bloom = pool.texture(m_bloom);
			auto& sceneColor = pool.texture(m_sceneColor);

			m_mipViews.resize(BLOOM_MIP_LEVELS);
			for (uint32_t i = 0; i < BLOOM_MIP_LEVELS; i++)
			{
				ImageView::CreateInfo viewInfo{
					.baseMipLevel = i,
					.levelCount = 1,
				};
				m_mipViews[i] = ImageView{ viewInfo, bloom.image() };
			}

			DescriptorWriter{ m_thresholdSetLayout }
				.writeImage(0, VkDescriptorImageInfo{ VK_NULL_HANDLE, sceneColor.view(), VK_IMAGE_LAYOUT_GENERAL })
				.writeImage(1, VkDescriptorImageInfo{ VK_NULL_HANDLE, m_mipViews[0], VK_IMAGE_LAYOUT_GENERAL })
				.update(m_thresholdSet);

			for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; i++)
			{
				DescriptorWriter{ m_upsampleSetLayout }
					.writeImage(0, VkDescriptorImageInfo{ VK_NULL_HANDLE, m_mipViews[i], VK_IMAGE_LAYOUT_GENERAL })
					.writeImage(1, VkDescriptorImageInfo{ m_sampler, m_mipViews[i + 1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL })
					.update(m_upsampleSets[i]);

				DescriptorWriter{ m_downsampleSetLayout }
					.writeImage(0, VkDescriptorImageInfo{ VK_NULL_HANDLE, m_mipViews[i + 1], VK_IMAGE_LAYOUT_GENERAL })
					.writeImage(1, VkDescriptorImageInfo{ m_sampler, m_mipViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL })
					.update(m_downsampleSets[i]);
			}
		}

		virtual void execute(FGResourcePool& resources, const FrameInfo& frameInfo) override
		{
			VkCommandBuffer cmd = frameInfo.cmd;
			auto& bloom = pool.texture(m_bloom);

			extractBrightRegions(cmd, frameInfo);
			downSample(cmd, bloom);
			upSample(cmd, bloom);
		}

		virtual void drawUI() override
		{
			ImGui::SliderFloat("Threshold", &m_threshold.threshold, 0.0f, 2.0f);
			ImGui::SliderFloat("Soft Threshold", &m_threshold.softThreshold, 0.0f, 2.0f);
			ImGui::SliderFloat("Downsample Scale", &m_downsample.filterScale, 0.0f, 2.0f);
			ImGui::SliderFloat("Upsample Scale", &m_upsample.filterScale, 0.0f, 2.0f);
		}

	private:
		auto createThresholdDescriptorSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.build();
		}

		auto createDownsampleDescriptorSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build();
		}

		auto createUpsampleDescriptorSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build();
		}

		void extractBrightRegions(VkCommandBuffer cmd, const FrameInfo& frameInfo)
		{
			m_thresholdPipeline->bind(cmd);
			m_thresholdPipeline->bindDescriptorSet(cmd, 0, m_thresholdSet);
			m_thresholdPipeline->pushConstants(cmd, VK_SHADER_STAGE_COMPUTE_BIT, m_threshold);

			Tools::vk::cmdDispatch(cmd, frameInfo.swapChainExtent, { 16, 16 });
		}

		void downSample(VkCommandBuffer cmd, Texture& bloom)
		{
			AGX_ASSERT(bloom.image().layout() == VK_IMAGE_LAYOUT_GENERAL);

			m_downsamplePipeline->bind(cmd);

			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = bloom.image();
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.subresourceRange.levelCount = 1;

			for (uint32_t i = 1; i < BLOOM_MIP_LEVELS; i++)
			{
				uint32_t srcMip = i - 1;
				uint32_t dstMip = i;

				barrier.subresourceRange.baseMipLevel = srcMip;
				barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
					0, nullptr,
					0, nullptr,
					1, &barrier
				);

				m_downsample.mipLevel = srcMip;

				m_downsamplePipeline->bindDescriptorSet(cmd, 0, m_downsampleSets[srcMip]);
				m_downsamplePipeline->pushConstants(cmd, VK_SHADER_STAGE_COMPUTE_BIT, m_downsample);

				VkExtent2D mipExtent = { bloom.image().width() >> dstMip, bloom.image().height() >> dstMip };
				Tools::vk::cmdDispatch(cmd, mipExtent, { 16, 16 });
			}

			barrier.subresourceRange.baseMipLevel = BLOOM_MIP_LEVELS - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
		}

		void upSample(VkCommandBuffer cmd, Texture& bloom)
		{
			m_upsamplePipeline->bind(cmd);

			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = bloom.image();
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.subresourceRange.levelCount = 1;

			for (uint32_t i = BLOOM_MIP_LEVELS - 1; i > 0; i--)
			{
				uint32_t srcMip = i;
				uint32_t dstMip = i - 1;

				// Transition the dst mip level to general (write)
				barrier.subresourceRange.baseMipLevel = dstMip;
				barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
					0, nullptr,
					0, nullptr,
					1, &barrier
				);

				// Upsample the mip level
				m_upsamplePipeline->bindDescriptorSet(cmd, 0, m_upsampleSets[dstMip]);
				m_upsamplePipeline->pushConstants(cmd, VK_SHADER_STAGE_COMPUTE_BIT, m_upsample);

				VkExtent2D mipExtent = { bloom.image().width() >> dstMip, bloom.image().height() >> dstMip };
				Tools::vk::cmdDispatch(cmd, mipExtent, { 16, 16 });

				// Transition the dst mip level back to shader read 
				barrier.subresourceRange.baseMipLevel = dstMip;
				barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				vkCmdPipelineBarrier(cmd,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
					0, nullptr,
					0, nullptr,
					1, &barrier
				);
			}

			// Transition all mip levels back to general
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = BLOOM_MIP_LEVELS;
			barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
		}

		FGResourceHandle m_sceneColor;
		FGResourceHandle m_bloom;

		std::vector<ImageView> m_mipViews;
		Sampler m_sampler;

		BloomThreshold m_threshold;
		BloomDownsample m_downsample;
		BloomUpsample m_upsample;

		DescriptorSetLayout m_thresholdSetLayout;
		DescriptorSet m_thresholdSet;
		std::unique_ptr<Pipeline> m_thresholdPipeline;

		DescriptorSetLayout m_downsampleSetLayout;
		std::vector<DescriptorSet> m_downsampleSets;
		std::unique_ptr<Pipeline> m_downsamplePipeline;

		DescriptorSetLayout m_upsampleSetLayout;
		std::vector<DescriptorSet> m_upsampleSets;
		std::unique_ptr<Pipeline> m_upsamplePipeline;
	};
}