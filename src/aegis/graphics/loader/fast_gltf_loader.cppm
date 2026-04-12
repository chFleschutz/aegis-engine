module;

#include "core/assert.h"
#include "graphics/vulkan/vulkan_include.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

export module Aegis.Graphics.Loader:FastGLTFLoader;

import Aegis.Math;
import Aegis.Graphics.StaticMesh;
import Aegis.Graphics.Texture;
import Aegis.Graphics.MaterialTemplate;
import Aegis.Graphics.MaterialInstance;
import Aegis.Graphics.MeshPreprocessor;
import Aegis.Graphics.Components;
import Aegis.Scene.Registry;
import Aegis.Core.AssetManager;

export namespace Aegis::Graphics
{
	class FastGLTFLoader
	{
	public:
		FastGLTFLoader(Scene::Registry& scene, const std::filesystem::path& path)
		{
			auto data = fastgltf::GltfDataBuffer::FromPath(path);
			if (data.error() != fastgltf::Error::None)
			{
				AGX_UNREACHABLE("Failed to load GLTF file data");
				return;
			}

			m_basePath = path.parent_path();
			auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DecomposeNodeMatrices;
			auto asset = parser.loadGltf(data.get(), m_basePath, options);
			if (auto error = asset.error(); error != fastgltf::Error::None)
			{
				AGX_UNREACHABLE("Failed to parse GLTF asset");
				return;
			}

			// Get default assets
			m_pbrTemplate = Core::AssetManager::instance().get<Graphics::MaterialTemplate>("default/PBR_template");
			m_pbrDefaultMat = Core::AssetManager::instance().get<Graphics::MaterialInstance>("default/PBR_instance");

			auto& gltf = asset.get();
			loadMeshes(gltf);
			loadTextures(gltf);
			loadMaterials(gltf);

			size_t startScene = gltf.defaultScene.value_or(0);
			m_rootEntity = scene.create(gltf.scenes[startScene].name.empty()
				? path.stem().string()
				: std::string(gltf.scenes[startScene].name));

			// Correct coordinate system (GLTF uses Y-up, Z-forward)
			scene.get<Transform>(m_rootEntity).rotation = glm::radians(glm::vec3{ 90.0f, 0.0f, 0.0f });

			buildScene(scene, gltf, startScene);
		}

		[[nodiscard]] auto rootEntity() const -> Scene::Entity { return m_rootEntity; }

	private:
		inline static fastgltf::Parser parser;

		void loadMeshes(const fastgltf::Asset& gltf)
		{
			m_meshCache.resize(gltf.meshes.size());

			for (size_t i = 0; i < gltf.meshes.size(); ++i)
			{
				auto& mesh = gltf.meshes[i];
				m_meshCache[i].reserve(mesh.primitives.size());

				for (const auto& primitive : mesh.primitives)
				{
					Graphics::MeshPreprocessor::Input input{};

					auto* posIt = primitive.findAttribute("POSITION");
					AGX_ASSERT_X(posIt != primitive.attributes.end(), "GLTF primitive is missing POSITION attribute");
					auto& positionAcc = gltf.accessors[posIt->accessorIndex];
					input.positions.reserve(positionAcc.count);
					fastgltf::iterateAccessor<fastgltf::math::fvec3>(gltf, positionAcc, [&](fastgltf::math::fvec3 pos)
						{
							input.positions.emplace_back(glm::vec3{ pos.x(), pos.y(), pos.z() });
						});

					auto* normIt = primitive.findAttribute("NORMAL");
					AGX_ASSERT_X(normIt != primitive.attributes.end(), "GLTF primitive is missing NORMAL attribute");
					auto& normalAcc = gltf.accessors[normIt->accessorIndex];
					input.normals.reserve(normalAcc.count);
					fastgltf::iterateAccessor<fastgltf::math::fvec3>(gltf, normalAcc, [&](fastgltf::math::fvec3 norm)
						{
							input.normals.emplace_back(glm::vec3{ norm.x(), norm.y(), norm.z() });
						});

					auto* uvIt = primitive.findAttribute("TEXCOORD_0");
					if (uvIt != primitive.attributes.end())
					{
						auto& uvAcc = gltf.accessors[uvIt->accessorIndex];
						input.uvs.reserve(uvAcc.count);
						fastgltf::iterateAccessor<fastgltf::math::fvec2>(gltf, uvAcc, [&](fastgltf::math::fvec2 uv)
							{
								input.uvs.emplace_back(glm::vec2{ uv.x(), uv.y() });
							});
					}

					auto* colorIt = primitive.findAttribute("COLOR_0");
					if (colorIt != primitive.attributes.end())
					{
						auto& colorAcc = gltf.accessors[colorIt->accessorIndex];
						input.colors.reserve(colorAcc.count);
						fastgltf::iterateAccessor<fastgltf::math::fvec3>(gltf, colorAcc, [&](fastgltf::math::fvec3 color)
							{
								input.colors.emplace_back(glm::vec3{ color.x(), color.y(), color.z() });
							});
					}

					if (primitive.indicesAccessor.has_value())
					{
						auto& indexAcc = gltf.accessors[*primitive.indicesAccessor];
						input.indices.reserve(indexAcc.count);
						fastgltf::iterateAccessor<uint32_t>(gltf, indexAcc, [&](uint32_t index)
							{
								input.indices.emplace_back(index);
							});
					}

					auto meshInfo = Graphics::MeshPreprocessor::process(input);
					m_meshCache[i].emplace_back(std::make_shared<Graphics::StaticMesh>(meshInfo));
				}
			}
		}

		void loadTextures(const fastgltf::Asset& gltf)
		{
			// Pre-scan materials to determine texture formats
			m_textureFormats.resize(gltf.textures.size(), VK_FORMAT_R8G8B8A8_UNORM);
			for (const auto& material : gltf.materials)
			{
				if (material.pbrData.baseColorTexture.has_value())
				{
					m_textureFormats[material.pbrData.baseColorTexture->textureIndex] = VK_FORMAT_R8G8B8A8_SRGB;
				}
				if (material.pbrData.metallicRoughnessTexture.has_value())
				{
					m_textureFormats[material.pbrData.metallicRoughnessTexture->textureIndex] = VK_FORMAT_R8G8B8A8_UNORM;
				}
				if (material.normalTexture.has_value())
				{
					m_textureFormats[material.normalTexture->textureIndex] = VK_FORMAT_R8G8B8A8_UNORM;
				}
				if (material.occlusionTexture.has_value())
				{
					m_textureFormats[material.occlusionTexture->textureIndex] = VK_FORMAT_R8G8B8A8_UNORM;
				}
				if (material.emissiveTexture.has_value())
				{
					m_textureFormats[material.emissiveTexture->textureIndex] = VK_FORMAT_R8G8B8A8_SRGB;
				}
			}

			// Load textures
			m_textureCache.reserve(gltf.textures.size());
			for (size_t i = 0; i < gltf.textures.size(); ++i)
			{
				const auto& texture = gltf.textures[i];
				if (!texture.imageIndex.has_value())
					continue;

				const auto& image = gltf.images[texture.imageIndex.value()];
				std::visit(fastgltf::visitor{
					[](auto&) { AGX_UNREACHABLE("Unsupported image data source");  },
					[&](const fastgltf::sources::URI& uri)
					{
						m_textureCache.emplace_back(Graphics::Texture::loadFromFile(
							m_basePath / uri.uri.path(), m_textureFormats[i]));
					},
					[&](const fastgltf::sources::BufferView& view)
					{
						const auto& bufferView = gltf.bufferViews[view.bufferViewIndex];
						const auto& buffer = gltf.buffers[bufferView.bufferIndex];
						const auto& data = std::get<fastgltf::sources::Array>(buffer.data);
						const auto* imagePtr = data.bytes.data() + bufferView.byteOffset;
						m_textureCache.emplace_back(Graphics::Texture::loadFromMemory(
							imagePtr, bufferView.byteLength, m_textureFormats[i]));
					},
					}, image.data);
			}
		}

		void loadMaterials(const fastgltf::Asset& gltf)
		{
			m_materialCache.reserve(gltf.materials.size());
			for (size_t i = 0; i < gltf.materials.size(); ++i)
			{
				const auto& gltfMat = gltf.materials[i];

				auto materialInstance = Graphics::MaterialInstance::create(m_pbrTemplate);
				materialInstance->setParameter("albedo", glm::make_vec3(gltfMat.pbrData.baseColorFactor.data()));
				materialInstance->setParameter("metallic", gltfMat.pbrData.metallicFactor);
				materialInstance->setParameter("roughness", gltfMat.pbrData.roughnessFactor);
				materialInstance->setParameter("emissive", glm::make_vec3(gltfMat.emissiveFactor.data()));

				if (gltfMat.pbrData.baseColorTexture.has_value())
				{
					auto texIdx = gltfMat.pbrData.baseColorTexture->textureIndex;
					materialInstance->setParameter("albedoMap", m_textureCache[texIdx]);
				}
				if (gltfMat.pbrData.metallicRoughnessTexture.has_value())
				{
					auto texIdx = gltfMat.pbrData.metallicRoughnessTexture->textureIndex;
					materialInstance->setParameter("metalRoughnessMap", m_textureCache[texIdx]);
				}
				if (gltfMat.normalTexture.has_value())
				{
					auto texIdx = gltfMat.normalTexture->textureIndex;
					materialInstance->setParameter("normalMap", m_textureCache[texIdx]);
				}
				if (gltfMat.occlusionTexture.has_value())
				{
					auto texIdx = gltfMat.occlusionTexture->textureIndex;
					materialInstance->setParameter("ambientOcclusionMap", m_textureCache[texIdx]);
				}
				if (gltfMat.emissiveTexture.has_value())
				{
					auto texIdx = gltfMat.emissiveTexture->textureIndex;
					materialInstance->setParameter("emissiveMap", m_textureCache[texIdx]);
				}

				m_materialCache.emplace_back(materialInstance);
			}
		}

		void buildScene(Scene::Registry& scene, const fastgltf::Asset& gltf, size_t sceneIndex)
		{
			auto& gltfScene = gltf.scenes[sceneIndex];

			// Create all entities first
			std::vector<Scene::Entity> entityCache(gltf.nodes.size());
			for (size_t i = 0; i < gltf.nodes.size(); ++i)
			{
				auto& node = gltf.nodes[i];
				auto& trs = std::get<fastgltf::TRS>(node.transform);

				auto entityName = node.name.empty() ? std::format("Node_{}", i) : std::string(node.name);
				auto location = glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() };
				auto rotation = glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() };
				auto scale = glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() };

				auto entity = scene.create(entityName, location, rotation, scale);
				entityCache[i] = entity;

				// Add mesh if exists
				if (node.meshIndex.has_value())
				{
					const auto& subMeshes = m_meshCache[*node.meshIndex];
					const auto& gltfMesh = gltf.meshes[*node.meshIndex];
					if (subMeshes.size() == 1) // Single mesh, add directly to entity
					{
						scene.add<Mesh>(entity, subMeshes[0]);
						scene.add<Material>(entity, queryMaterial(gltfMesh, 0));
					}
					else // Multiple submeshes, create child entities
					{
						for (size_t subIdx = 0; subIdx < subMeshes.size(); ++subIdx)
						{
							const auto& subMesh = subMeshes[subIdx];

							auto childEntity = scene.create(std::format("{}_Submesh_{}", entityName, subIdx));
							scene.add<Mesh>(childEntity, subMesh);
							scene.add<Material>(childEntity, queryMaterial(gltfMesh, subIdx));
							scene.addChild(entity, childEntity);
						}
					}
				}
			}

			// Setup parent-child relationships 
			for (size_t i = 0; i < gltf.nodes.size(); ++i)
			{
				const auto& node = gltf.nodes[i];
				Scene::Entity parent = entityCache[i];
				for (const auto& childIndex : node.children)
				{
					scene.addChild(parent, entityCache[childIndex]);
				}
			}

			// Add top level nodes to root 
			for (auto& entity : entityCache)
			{
				if (!scene.get<Parent>(entity).entity)
					scene.setParent(entity, m_rootEntity);
			}
		}

		auto queryMaterial(const fastgltf::Mesh& mesh, size_t subIdx) -> std::shared_ptr<Graphics::MaterialInstance>
		{
			const auto& matIndex = mesh.primitives[subIdx].materialIndex;
			return matIndex.has_value()
				? m_materialCache[matIndex.value()]
				: m_pbrDefaultMat;
		}

		Scene::Entity m_rootEntity;
		std::shared_ptr<Graphics::MaterialTemplate> m_pbrTemplate;
		std::shared_ptr<Graphics::MaterialInstance> m_pbrDefaultMat;
		std::filesystem::path m_basePath;
		std::vector<VkFormat> m_textureFormats;
		std::vector<std::shared_ptr<Graphics::Texture>> m_textureCache;
		std::vector<std::shared_ptr<Graphics::MaterialInstance>> m_materialCache;
		std::vector<std::vector<std::shared_ptr<Graphics::StaticMesh>>> m_meshCache;
	};
}