module;

#include "graphics/descriptors.h"
#include "graphics/frame_graph/frame_graph_render_pass.h"
#include "graphics/pipeline.h"

#include <vector>

export module Aegis.Graphics.RenderPasses.PostProcessingPass;

export namespace Aegis::Graphics
{
	enum class ToneMappingMode
	{
		Reinhard,
		ACES
	};

	struct PostProcessingSettings
	{
		ToneMappingMode toneMappingMode = ToneMappingMode::ACES;
		float bloomIntensity = 0.1f;
		float exposure = 1.0f;
		float gamma = 2.2f;
	};

	class PostProcessingPass : public FGRenderPass
	{
	public:
		PostProcessingPass(FGResourcePool& pool) :
			m_descriptorSetLayout{ createDescriptorSetLayout() },
			m_pipeline{ createPipeline() }
		{
			m_descriptorSets.reserve(MAX_FRAMES_IN_FLIGHT);
			for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				m_descriptorSets.emplace_back(m_descriptorSetLayout);
			}

			m_sceneColor = pool.addReference("SceneColor",
				FGResource::Usage::ComputeReadStorage);

			m_bloom = pool.addReference("Bloom",
				FGResource::Usage::ComputeReadStorage);

			m_final = pool.addImage("Final",
				FGResource::Usage::ComputeWriteStorage,
				FGTextureInfo{
					.format = VK_FORMAT_R8G8B8A8_UNORM,
					.resizeMode = FGResizeMode::SwapChainRelative,
				});
		}

		virtual auto info() -> FGNode::Info override
		{
			return FGNode::Info{
				.name = "Post Processing",
				.reads = { m_sceneColor, m_bloom },
				.writes = { m_final }
			};
		}

		virtual void createResources(FGResourcePool& resources) override {}
		virtual void execute(FGResourcePool& resources, const FrameInfo& frameInfo) override
		{
			// TODO: Dont update descriptors every frame
			DescriptorWriter{ m_descriptorSetLayout }
				.writeImage(0, pool.texture(m_final))
				.writeImage(1, pool.texture(m_sceneColor))
				.writeImage(2, pool.texture(m_bloom))
				.update(m_descriptorSets[frameInfo.frameIndex]);

			VkCommandBuffer cmd = frameInfo.cmd;

			m_pipeline.bind(cmd);
			m_pipeline.bindDescriptorSet(cmd, 0, m_descriptorSets[frameInfo.frameIndex]);
			m_pipeline.pushConstants(cmd, VK_SHADER_STAGE_COMPUTE_BIT, m_settings);

			Tools::vk::cmdDispatch(cmd, frameInfo.swapChainExtent, { 16, 16 });
		}

		virtual void drawUI() override
		{
			static constexpr auto toneMappingNames = std::array{ "Reinhard", "ACES" };
			int currentMode = static_cast<int>(m_settings.toneMappingMode);
			if (ImGui::Combo("Tone Mapping Mode", &currentMode, toneMappingNames.data(), toneMappingNames.size()))
			{
				m_settings.toneMappingMode = static_cast<ToneMappingMode>(currentMode);
			}

			ImGui::SliderFloat("Bloom Intensity", &m_settings.bloomIntensity, 0.0f, 2.0f);
			ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.1f, 10.0f);
			ImGui::SliderFloat("Gamma", &m_settings.gamma, 1.0f, 5.0f);
		}

	private:
		auto createDescriptorSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.build();
		}

		auto createPipeline() -> Pipeline
		{
			return Pipeline::ComputeBuilder{}
				.addDescriptorSetLayout(m_descriptorSetLayout)
				.addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(PostProcessingSettings))
				.setShaderStage(SHADER_DIR "post_process.slang.spv")
				.build();
		}

		FGResourceHandle m_sceneColor;
		FGResourceHandle m_bloom;
		FGResourceHandle m_final;

		PostProcessingSettings m_settings;

		DescriptorSetLayout m_descriptorSetLayout;
		std::vector<DescriptorSet> m_descriptorSets;
		Pipeline m_pipeline;
	};
}