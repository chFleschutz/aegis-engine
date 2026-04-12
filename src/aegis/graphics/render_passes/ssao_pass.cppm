module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <imgui/imgui.h>

#include <memory>
#include <random>
#include <vector>

export module Aegis.Graphics.RenderPasses.SSAOPass;

import Aegis.Math;
import Aegis.Core.Globals;
import Aegis.Graphics.Descriptors;
import Aegis.Graphics.FrameGraph.RenderPass;
import Aegis.Graphics.Pipeline;
import Aegis.Graphics.Buffer;
import Aegis.Graphics.Texture;
import Aegis.Graphics.Vulkan.Tools;

export namespace Aegis::Graphics
{
	struct SSAOUniforms
	{
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::vec2 noiseScale{ 4.0f };
		float radius{ 0.5f };
		float bias{ 0.025 };
		float power{ 2.0f };
	};

	class SSAOPass : public FGRenderPass
	{
	public:
		inline static constexpr uint32_t SAMPLE_COUNT = 64;
		inline static constexpr uint32_t NOISE_SIZE = 16;

		SSAOPass(FGResourcePool& pool)
			: m_uniforms{ Buffer::uniformBuffer(sizeof(SSAOUniforms)) },
			m_ssaoSamples{ Buffer::uniformBuffer(sizeof(glm::vec4) * SAMPLE_COUNT, 1) }
		{
			m_descriptorSetLayout = DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.buildUnique();

			m_descriptorSet = std::make_unique<DescriptorSet>(*m_descriptorSetLayout);

			m_pipeline = Pipeline::ComputeBuilder{}
				.addDescriptorSetLayout(*m_descriptorSetLayout)
				.setShaderStage(Core::SHADER_DIR / "ssao.comp.spv")
				.buildUnique();

			// Generate random samples
			auto& gen = Math::Random::generator();
			std::uniform_real_distribution dis(0.0f, 1.0f);
			std::vector<glm::vec4> samples;
			samples.reserve(SAMPLE_COUNT);
			for (uint32_t i = 0; i < SAMPLE_COUNT; i++)
			{
				glm::vec4 sample{
					dis(gen) * 2.0f - 1.0f,
					dis(gen) * 2.0f - 1.0f,
					dis(gen),
					0.0f
				};
				sample = glm::normalize(sample);
				sample *= dis(gen);

				float scale = static_cast<float>(i) / static_cast<float>(SAMPLE_COUNT);
				sample *= Math::lerp(0.1f, 1.0f, scale * scale);
				samples.emplace_back(sample);
			}

			m_ssaoSamples.singleWrite(samples.data());

			// Generate noise texture
			std::vector<glm::vec2> noise(NOISE_SIZE * NOISE_SIZE);
			for (auto& n : noise)
			{
				n.x = dis(gen) * 2.0f - 1.0f;
				n.y = dis(gen) * 2.0f - 1.0f;
			}

			auto texInfo = Texture::CreateInfo::texture2D(NOISE_SIZE, NOISE_SIZE, VK_FORMAT_R16G16_UNORM);
			texInfo.image.mipLevels = 1;
			m_ssaoNoise = Texture{ texInfo };
			m_ssaoNoise.image().upload(noise.data(), sizeof(glm::vec2) * noise.size());
			m_ssaoNoise.image().transitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			m_position = pool.addReference("Position",
				FGResource::Usage::ComputeReadSampled);

			m_normal = pool.addReference("Normal",
				FGResource::Usage::ComputeReadSampled);

			m_ssao = pool.addImage("SSAO",
				FGResource::Usage::ComputeWriteStorage,
				FGTextureInfo{
					.format = VK_FORMAT_R8_UNORM,
					.extent = { 0, 0 },
					.resizeMode = FGResizeMode::SwapChainRelative
				});
		}

		virtual auto info() -> Info override
		{
			return Info{
				.name = "SSAO",
				.reads = { m_position, m_normal },
				.writes = { m_ssao }
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			VkCommandBuffer cmd = frameInfo.cmd;
			auto& registry = frameInfo.scene.registry();

			// Update push constants
			auto& camera = registry.get<Camera>(frameInfo.scene.mainCamera());
			m_uniformData.view = camera.viewMatrix;
			m_uniformData.projection = camera.projectionMatrix;
			m_uniformData.noiseScale.x = m_uniformData.noiseScale.y * camera.aspect;
			m_uniforms.writeToIndex(&m_uniformData, frameInfo.frameIndex);

			DescriptorWriter{ *m_descriptorSetLayout }
				.writeImage(0, pool.texture(m_ssao).descriptorImageInfo())
				.writeImage(1, pool.texture(m_position).descriptorImageInfo())
				.writeImage(2, pool.texture(m_normal).descriptorImageInfo())
				.writeImage(3, m_ssaoNoise.descriptorImageInfo())
				.writeBuffer(4, m_ssaoSamples.descriptorBufferInfo())
				.writeBuffer(5, m_uniforms.descriptorBufferInfoFor(frameInfo.frameIndex))
				.update(*m_descriptorSet);

			m_pipeline->bind(cmd);
			m_descriptorSet->bind(cmd, m_pipeline->layout(), VK_PIPELINE_BIND_POINT_COMPUTE);

			Tools::vk::cmdDispatch(cmd, frameInfo.swapChainExtent, { 16, 16 });
		}

		virtual void drawUI() override
		{
			ImGui::DragFloat("Noise Scale", &m_uniformData.noiseScale.y, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat("Radius", &m_uniformData.radius, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("Bias", &m_uniformData.bias, 0.001f, 0.0f, 1.0f);
			ImGui::DragFloat("Power", &m_uniformData.power, 0.01f, 0.0f, 10.0f);
		}

	private:
		FGResourceHandle m_position;
		FGResourceHandle m_normal;
		FGResourceHandle m_ssao;

		std::unique_ptr<Pipeline> m_pipeline;
		std::unique_ptr<DescriptorSetLayout> m_descriptorSetLayout;
		std::unique_ptr<DescriptorSet> m_descriptorSet;

		Texture m_ssaoNoise;
		Buffer m_ssaoSamples;
		Buffer m_uniforms;

		SSAOUniforms m_uniformData;
	};
}