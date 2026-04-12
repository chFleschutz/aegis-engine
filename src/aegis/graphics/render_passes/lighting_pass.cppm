module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <imgui/imgui.h>

#include <array>
#include <memory>
#include <vector>

export module Aegis.Graphics.RenderPasses.LightingPass;

import Aegis.Math;
import Aegis.Core.Globals;
import Aegis.Graphics.FrameGraph.RenderPass;
import Aegis.Graphics.Pipeline;
import Aegis.Graphics.Descriptors;
import Aegis.Graphics.Globals;
import Aegis.Graphics.Vulkan.Tools;
import Aegis.Graphics.Buffer;
import Aegis.Graphics.Globals;
import Aegis.Graphics.Components;
import Aegis.Scene.Components;

export namespace Aegis::Graphics
{
	enum class LightingViewMode : int32_t
	{
		SceneColor = 0,
		Albedo = 1,
		AmbientOcclusion = 2,
		Roughness = 3,
		Metallic = 4,
		Emissive = 5,
	};

	struct LightingUniforms
	{
		struct AmbientLight
		{
			glm::vec4 color{ 0.0f };
		};

		struct DirectionalLight
		{
			glm::vec4 direction{ 0.0f, 0.0f, 1.0f, 0.0f };
			glm::vec4 color{ 0.0f };
		};

		struct PointLight
		{
			glm::vec4 position{ 0.0f };
			glm::vec4 color{ 0.0f };
		};

		glm::vec4 cameraPosition{ 0.0f };
		AmbientLight ambient{};
		DirectionalLight directional{};
		std::array<PointLight, MAX_POINT_LIGHTS> pointLights{};
		int32_t pointLightCount{ 0 };
		float ambientOcclusionFactor{ 0.5f };
		LightingViewMode viewMode{ LightingViewMode::SceneColor };
	};

	class LightingPass : public FGRenderPass
	{
	public:
		LightingPass(FGResourcePool& pool) :
			m_ubo{ Buffer::uniformBuffer(sizeof(LightingUniforms)) },
			m_gbufferSetLayout{ createGBufferSetLayout() },
			m_iblSetLayout{ createIBLSetLayout() }
		{
			m_gbufferSets.reserve(MAX_FRAMES_IN_FLIGHT);
			m_iblSets.reserve(MAX_FRAMES_IN_FLIGHT);
			for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
			{
				m_gbufferSets.emplace_back(m_gbufferSetLayout);
				m_iblSets.emplace_back(m_iblSetLayout);
			}

			m_pipeline = Pipeline::ComputeBuilder{}
				.addDescriptorSetLayout(m_gbufferSetLayout)
				.addDescriptorSetLayout(m_iblSetLayout)
				.setShaderStage(Core::SHADER_DIR / "pbr_lighting.slang.spv", "computeMain")
				.buildUnique();

			m_position = pool.addReference("Position",
				FGResource::Usage::ComputeReadStorage);

			m_normal = pool.addReference("Normal",
				FGResource::Usage::ComputeReadStorage);

			m_albedo = pool.addReference("Albedo",
				FGResource::Usage::ComputeReadStorage);

			m_arm = pool.addReference("ARM",
				FGResource::Usage::ComputeReadStorage);

			m_emissive = pool.addReference("Emissive",
				FGResource::Usage::ComputeReadStorage);

			//m_ssao = pool.addReference("SSAO",
			//	FGResource::Usage::ComputeReadStorage);

			m_sceneColor = pool.addImage("SceneColor",
				FGResource::Usage::ComputeWriteStorage,
				FGTextureInfo{
					.format = VK_FORMAT_R16G16B16A16_SFLOAT,
					.resizeMode = FGResizeMode::SwapChainRelative
				});
		}

		virtual auto info() -> Info override
		{
			return Info{
				.name = "Lighting",
				.reads = { m_position, m_normal, m_albedo, m_arm, m_emissive/*, m_ssao*/ },
				.writes = { m_sceneColor }
			};
		}

		virtual void execute(FGResourcePool& pool, const FrameInfo& frameInfo) override
		{
			VkCommandBuffer cmd = frameInfo.cmd;

			updateLightingUBO(frameInfo);
			auto& registry = frameInfo.scene.registry();
			auto& environment = registry.get<Environment>(frameInfo.scene.environment());
			AGX_ASSERT_X(environment.irradiance, "Environment irradiance map is not set");

			DescriptorWriter{ m_gbufferSetLayout }
				.writeImage(0, pool.texture(m_sceneColor).descriptorImageInfo())
				.writeImage(1, pool.texture(m_position).descriptorImageInfo())
				.writeImage(2, pool.texture(m_normal).descriptorImageInfo())
				.writeImage(3, pool.texture(m_albedo).descriptorImageInfo())
				.writeImage(4, pool.texture(m_arm).descriptorImageInfo())
				.writeImage(5, pool.texture(m_emissive).descriptorImageInfo())
				//.writeImage(6, pool.texture(m_ssao))
				.writeBuffer(7, m_ubo.descriptorBufferInfoFor(frameInfo.frameIndex))
				.update(m_gbufferSets[frameInfo.frameIndex]);

			DescriptorWriter{ m_iblSetLayout }
				.writeImage(0, environment.irradiance->descriptorImageInfo())
				.writeImage(1, environment.prefiltered->descriptorImageInfo())
				.writeImage(2, environment.brdfLUT->descriptorImageInfo())
				.update(m_iblSets[frameInfo.frameIndex]);

			m_pipeline->bind(cmd);
			m_pipeline->bindDescriptorSet(cmd, 0, m_gbufferSets[frameInfo.frameIndex]);
			m_pipeline->bindDescriptorSet(cmd, 1, m_iblSets[frameInfo.frameIndex]);

			Tools::vk::cmdDispatch(cmd, frameInfo.swapChainExtent, { 16, 16 });
		}

		virtual void drawUI() override
		{
			ImGui::DragFloat("AO Factor", &m_ambientOcclusionFactor, 0.01f, 0.0f, 1.0f);

			static constexpr auto viewModeNames = std::array{ "Scene Color", "Albedo", "Ambient Occlusion" , "Roughness",
				"Metallic",	"Emissive" };
			int currentMode = static_cast<int>(m_viewMode);
			if (ImGui::Combo("View Mode", &currentMode, viewModeNames.data(), viewModeNames.size()))
			{
				m_viewMode = static_cast<LightingViewMode>(currentMode);
			}
		}

	private:
		auto createGBufferSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build();
		}

		auto createIBLSetLayout() -> DescriptorSetLayout
		{
			return DescriptorSetLayout::Builder{}
				.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
				.build();
		}

		void updateLightingUBO(const FrameInfo& frameInfo)
		{
			auto& registry = frameInfo.scene.registry();
			LightingUniforms lighting;

			Scene::Entity mainCamera = frameInfo.scene.mainCamera();
			if (mainCamera && registry.has<Transform>(mainCamera))
			{
				auto& cameraTransform = registry.get<Transform>(mainCamera);
				lighting.cameraPosition = glm::vec4(cameraTransform.location, 1.0f);
			}

			Scene::Entity ambientLight = frameInfo.scene.ambientLight();
			if (ambientLight && registry.has<AmbientLight>(ambientLight))
			{
				auto& ambient = registry.get<AmbientLight>(ambientLight);
				lighting.ambient.color = glm::vec4(ambient.color, ambient.intensity);
			}

			Scene::Entity directionalLight = frameInfo.scene.directionalLight();
			if (directionalLight && registry.has<DirectionalLight, Transform>(directionalLight))
			{
				auto& directional = registry.get<DirectionalLight>(directionalLight);
				auto& transform = registry.get<Transform>(directionalLight);
				lighting.directional.color = glm::vec4(directional.color, directional.intensity);
				lighting.directional.direction = glm::vec4(glm::normalize(transform.forward()), 0.0f);
			}

			int32_t lighIndex = 0;
			auto view = frameInfo.scene.registry().view<Transform, PointLight>();
			for (auto&& [entity, transform, pointLight] : view.each())
			{
				AGX_ASSERT_X(lighIndex < MAX_POINT_LIGHTS, "Point lights exceed maximum number of point lights");
				lighting.pointLights[lighIndex] = LightingUniforms::PointLight{
					.position = glm::vec4(transform.location, 1.0f),
					.color = glm::vec4(pointLight.color, pointLight.intensity)
				};
				lighIndex++;
			}
			lighting.pointLightCount = lighIndex;

			lighting.ambientOcclusionFactor = m_ambientOcclusionFactor;
			lighting.viewMode = m_viewMode;

			m_ubo.writeToIndex(&lighting, frameInfo.frameIndex);
		}

		FGResourceHandle m_sceneColor;
		FGResourceHandle m_position;
		FGResourceHandle m_normal;
		FGResourceHandle m_albedo;
		FGResourceHandle m_arm;
		FGResourceHandle m_emissive;
		FGResourceHandle m_ssao;

		LightingViewMode m_viewMode{ LightingViewMode::SceneColor };
		float m_ambientOcclusionFactor{ 1.0f };

		std::unique_ptr<Pipeline> m_pipeline;

		DescriptorSetLayout m_gbufferSetLayout;
		DescriptorSetLayout m_iblSetLayout;
		std::vector<DescriptorSet> m_gbufferSets;
		std::vector<DescriptorSet> m_iblSets;
		Buffer m_ubo;
	};
}