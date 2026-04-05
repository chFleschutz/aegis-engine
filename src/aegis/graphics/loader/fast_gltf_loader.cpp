#include "pch.h"
#include "fast_gltf_loader.h"

#include "engine.h"
#include "graphics/resources/mesh_preprocessor.h"
#include "scene/components.h"

namespace Aegis::Scene
{
	FastGLTFLoader::FastGLTFLoader(Scene& scene, const std::filesystem::path& path)
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
		m_pbrTemplate = Engine::assets().get<Graphics::MaterialTemplate>("default/PBR_template");
		m_pbrDefaultMat = Engine::assets().get<Graphics::MaterialInstance>("default/PBR_instance");

		auto& gltf = asset.get();
		loadMeshes(gltf);
		loadTextures(gltf);
		loadMaterials(gltf);

		size_t startScene = gltf.defaultScene.value_or(0);
		m_rootEntity = scene.createEntity(gltf.scenes[startScene].name.empty()
			? path.stem().string()
			: std::string(gltf.scenes[startScene].name));

		// Correct coordinate system (GLTF uses Y-up, Z-forward)
		m_rootEntity.get<Transform>().rotation = glm::radians(glm::vec3{ 90.0f, 0.0f, 0.0f });

		buildScene(scene, gltf, startScene);
	}

	void FastGLTFLoader::loadMeshes(const fastgltf::Asset& gltf)
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

	void FastGLTFLoader::loadTextures(const fastgltf::Asset& gltf)
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

	void FastGLTFLoader::loadMaterials(const fastgltf::Asset& gltf)
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

	void FastGLTFLoader::buildScene(Scene& scene, const fastgltf::Asset& gltf, size_t sceneIndex)
	{
		auto& gltfScene = gltf.scenes[sceneIndex];

		// Create all entities first
		std::vector<Entity> entityCache(gltf.nodes.size());
		for (size_t i = 0; i < gltf.nodes.size(); ++i)
		{
			auto& node = gltf.nodes[i];
			auto& trs = std::get<fastgltf::TRS>(node.transform);

			auto entityName = node.name.empty() ? std::format("Node_{}", i) : std::string(node.name);
			auto location = glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() };
			auto rotation = glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() };
			auto scale = glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() };

			auto entity = scene.createEntity(entityName, location, rotation, scale);
			entityCache[i] = entity;

			// Add mesh if exists
			if (node.meshIndex.has_value())
			{
				const auto& subMeshes = m_meshCache[*node.meshIndex];
				const auto& gltfMesh = gltf.meshes[*node.meshIndex];
				if (subMeshes.size() == 1) // Single mesh, add directly to entity
				{
					entity.add<Mesh>(subMeshes[0]);
					entity.add<Material>(queryMaterial(gltfMesh, 0));
				}
				else // Multiple submeshes, create child entities
				{
					for (size_t subIdx = 0; subIdx < subMeshes.size(); ++subIdx)
					{
						const auto& subMesh = subMeshes[subIdx];

						auto childEntity = scene.createEntity(std::format("{}_Submesh_{}", entityName, subIdx));
						childEntity.add<Mesh>(subMesh);
						childEntity.add<Material>(queryMaterial(gltfMesh, subIdx));
						entity.addChild(childEntity);
					}
				}
			}
		}

		// Setup parent-child relationships 
		for (size_t i = 0; i < gltf.nodes.size(); ++i)
		{
			const auto& node = gltf.nodes[i];
			Entity parent = entityCache[i];
			for (const auto& childIndex : node.children)
			{
				parent.addChild(entityCache[childIndex]);
			}
		}

		// Add top level nodes to root 
		for (auto& entity : entityCache)
		{
			if (!entity.get<Parent>().entity)
				entity.setParent(m_rootEntity);
		}
	}

	auto FastGLTFLoader::queryMaterial(const fastgltf::Mesh& mesh, size_t subIdx) -> std::shared_ptr<Graphics::MaterialInstance>
	{
		const auto& matIndex = mesh.primitives[subIdx].materialIndex;
		return matIndex.has_value()
			? m_materialCache[matIndex.value()]
			: m_pbrDefaultMat;
	}
}