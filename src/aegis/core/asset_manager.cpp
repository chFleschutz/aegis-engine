#include "pch.h"
#include "asset_manager.h"

#include "core/globals.h"
#include "graphics/descriptors.h"
#include "graphics/pipeline.h"
#include "graphics/material/material_instance.h"
#include "graphics/material/material_template.h"
#include "engine.h"

namespace Aegis::Core
{
	void AssetManager::loadDefaultAssets()
	{
		using namespace Aegis::Graphics;

		// Default Textures

		add("default/texture_black", Texture::solidColor(glm::vec4{ 0.0f }));
		add("default/texture_white", Texture::solidColor(glm::vec4{ 1.0f }));
		add("default/texture_normal", Texture::solidColor(glm::vec4{ 0.5f, 0.5f, 1.0f, 0.0f }));

		add("default/cubemap_black", Texture::solidColorCube(glm::vec4{ 0.0f }));
		add("default/cubemap_white", Texture::solidColorCube(glm::vec4{ 1.0f }));

		// Default PBR Material
		{
			auto pipeline = []() {
				Pipeline::GraphicsBuilder builder{};
				builder.addDescriptorSetLayout(Engine::renderer().bindlessDescriptorSet().layout())
					.addPushConstantRange(VK_SHADER_STAGE_ALL, 128)
					.addColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT)
					.addColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT)
					.addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
					.addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
					.addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
					.setDepthAttachment(VK_FORMAT_D32_SFLOAT);
				if (Renderer::useGPUDrivenRendering())
				{
					return builder
						.addShaderStages(VK_SHADER_STAGE_TASK_BIT_EXT, 
							SHADER_DIR "gpu-driven/task_meshlet_cull.slang.spv")
						.addShaderStages(VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
							SHADER_DIR "gpu-driven/mesh_geometry_indirect.slang.spv")
						.addFlag(Pipeline::Flags::MeshShader)
						.build();
				}
				else
				{
					return builder
						.addShaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
							SHADER_DIR "cpu-driven/vertex_geometry_bindless.slang.spv")
						.build();
				}
			}();

			auto pbrMatTemplate = std::make_shared<MaterialTemplate>(std::move(pipeline));
			pbrMatTemplate->addParameter("albedo", glm::vec3{ 1.0f, 1.0f, 1.0f });
			pbrMatTemplate->addParameter("emissive", glm::vec3{ 0.0f, 0.0f, 0.0f });
			pbrMatTemplate->addParameter("metallic", 0.0f);
			pbrMatTemplate->addParameter("roughness", 1.0f);
			pbrMatTemplate->addParameter("ambientOcclusion", 1.0f);
			pbrMatTemplate->addParameter("albedoMap", get<Texture>("default/texture_white"));
			pbrMatTemplate->addParameter("normalMap", get<Texture>("default/texture_normal"));
			pbrMatTemplate->addParameter("metalRoughnessMap", get<Texture>("default/texture_white"));
			pbrMatTemplate->addParameter("ambientOcclusionMap", get<Texture>("default/texture_white"));
			pbrMatTemplate->addParameter("emissiveMap", get<Texture>("default/texture_white"));
			add("default/PBR_template", pbrMatTemplate);

			auto defaultPBRMaterial = Graphics::MaterialInstance::create(pbrMatTemplate);
			defaultPBRMaterial->setParameter("albedo", glm::vec3{ 0.8f, 0.8f, 0.9f });
			add("default/PBR_instance", defaultPBRMaterial);
		}
	}
}