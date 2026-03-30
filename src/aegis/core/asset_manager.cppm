module;

#include <cstdint>
#include <filesystem>
#include <unordered_map>

export module Aegis.Core.AssetManager;

import Aegis.Core.Asset;
import Aegis

export namespace Aegis::Core
{
	using AssetID = uint64_t;

	class AssetManager
	{
	public:
		AssetManager() = default;
		~AssetManager() = default;

		template<IsAsset T>
		[[nodiscard]] auto get(const std::filesystem::path& path) -> std::shared_ptr<T>
		{
			AssetID id = std::hash<std::filesystem::path>{}(path);
			auto it = m_assets.find(id);
			if (it != m_assets.end())
			{
				auto asset = std::dynamic_pointer_cast<T>(it->second);
				AGX_ASSERT_X(asset, "Asset found but type mismatch");
				return asset;
			}

			// TODO: Implement asset loading from file
			AGX_ASSERT_X(false, "Asset loading not implemented yet");
			return nullptr;
		}

		template<IsAsset T>
		void add(const std::filesystem::path& path, const std::shared_ptr<T>& asset)
		{
			AssetID id = std::hash<std::filesystem::path>{}(path);
			asset->m_path = path;
			m_assets[id] = asset;
		}

		void garbageCollect()
		{
			for (auto it = m_assets.begin(); it != m_assets.end(); )
			{
				if (it->second.use_count() == 1)
					it = m_assets.erase(it);
				else
					++it;
			}
		}

		void loadDefaultAssets()
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
						// Vertex shader
						return builder
							.addShaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
								SHADER_DIR "cpu-driven/vertex_geometry_bindless.slang.spv")
							.build();

						// Mesh shader
						//return builder	
						//	.addShaderStages(VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
						//		SHADER_DIR "cpu-driven/mesh_geometry_bindless.slang.spv")
						//	.addFlag(Pipeline::Flags::MeshShader)
						//	.build();

						// Mesh shader + task shader culling (Need to adjust StaticMesh::drawMeshlets to use group size of 32)
						//return builder
						//	.addShaderStages(VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
						//		SHADER_DIR "cpu-driven/mesh_geometry_cull.slang.spv")
						//	.addFlag(Pipeline::Flags::MeshShader)
						//	.build();
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

	private:
		// TODO: Use a weak_ptr for auto release of assets (needs asset file loading first to load on demand)
		std::unordered_map<AssetID, std::shared_ptr<Asset>> m_assets;
	};
}