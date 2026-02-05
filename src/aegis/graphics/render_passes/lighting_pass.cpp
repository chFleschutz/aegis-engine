#include "pch.h"
#include "lighting_pass.h"

#include "core/globals.h"
#include "graphics/vulkan/vulkan_context.h"
#include "graphics/vulkan/vulkan_tools.h"
#include "scene/components.h"

#include <imgui.h>

namespace Aegis::Graphics
{
	LightingPass::LightingPass(FGResourcePool& pool) :
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
			.setShaderStage(SHADER_DIR "pbr_lighting.slang.spv", "computeMain")
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

	auto LightingPass::info() -> FGNode::Info
	{
		return FGNode::Info{
			.name = "Lighting",
			.reads = { m_position, m_normal, m_albedo, m_arm, m_emissive/*, m_ssao*/ },
			.writes = { m_sceneColor }
		};
	}

	void LightingPass::execute(FGResourcePool& pool, const FrameInfo& frameInfo)
	{
		VkCommandBuffer cmd = frameInfo.cmd;

		updateLightingUBO(frameInfo);
		auto& environment = frameInfo.scene.environment().get<Environment>();
		AGX_ASSERT_X(environment.irradiance, "Environment irradiance map is not set");

		DescriptorWriter{ m_gbufferSetLayout }
			.writeImage(0, pool.texture(m_sceneColor))
			.writeImage(1, pool.texture(m_position))
			.writeImage(2, pool.texture(m_normal))
			.writeImage(3, pool.texture(m_albedo))
			.writeImage(4, pool.texture(m_arm))
			.writeImage(5, pool.texture(m_emissive))
			//.writeImage(6, pool.texture(m_ssao))
			.writeBuffer(7, m_ubo, frameInfo.frameIndex)
			.update(m_gbufferSets[frameInfo.frameIndex]);

		DescriptorWriter{ m_iblSetLayout }
			.writeImage(0, *environment.irradiance)
			.writeImage(1, *environment.prefiltered)
			.writeImage(2, *environment.brdfLUT)
			.update(m_iblSets[frameInfo.frameIndex]);

		m_pipeline->bind(cmd);
		m_pipeline->bindDescriptorSet(cmd, 0, m_gbufferSets[frameInfo.frameIndex]);
		m_pipeline->bindDescriptorSet(cmd, 1, m_iblSets[frameInfo.frameIndex]);

		Tools::vk::cmdDispatch(cmd, frameInfo.swapChainExtent, { 16, 16 });
	}

	void LightingPass::drawUI()
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

	auto LightingPass::createGBufferSetLayout() -> DescriptorSetLayout
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

	auto LightingPass::createIBLSetLayout() -> DescriptorSetLayout
	{
		return DescriptorSetLayout::Builder{}
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
			.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
			.build();
	}

	void LightingPass::updateLightingUBO(const FrameInfo& frameInfo)
	{
		LightingUniforms lighting;

		Scene::Entity mainCamera = frameInfo.scene.mainCamera();
		if (mainCamera && mainCamera.has<Transform>())
		{
			auto& cameraTransform = mainCamera.get<Transform>();
			lighting.cameraPosition = glm::vec4(cameraTransform.location, 1.0f);
		}

		Scene::Entity ambientLight = frameInfo.scene.ambientLight();
		if (ambientLight && ambientLight.has<AmbientLight>())
		{
			auto& ambient = ambientLight.get<AmbientLight>();
			lighting.ambient.color = glm::vec4(ambient.color, ambient.intensity);
		}

		Scene::Entity directionalLight = frameInfo.scene.directionalLight();
		if (directionalLight && directionalLight.has<DirectionalLight, Transform>())
		{
			auto& directional = directionalLight.get<DirectionalLight>();
			auto& transform = directionalLight.get<Transform>();
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
}