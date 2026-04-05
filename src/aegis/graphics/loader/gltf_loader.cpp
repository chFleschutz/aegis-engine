#include "pch.h"
#include "gltf_loader.h"

#include "engine.h"
#include "math/math.h"
#include "graphics/resources/mesh_preprocessor.h"

#include <gltf_utils.h>

#include <glm/gtc/type_ptr.hpp>

#include <variant>

namespace Aegis::Scene
{
	GLTFLoader::GLTFLoader(Scene& scene, const std::filesystem::path& path)
	{
		m_gltf = GLTF::load(path);
		AGX_ASSERT_X(m_gltf.has_value(), "Failed to load GLTF file");

		if (m_gltf->scenes.empty())
			return;

		m_meshes.resize(m_gltf->meshes.size());
		m_materials.resize(m_gltf->materials.size());
		m_textures.resize(m_gltf->textures.size());
		m_entities.reserve(m_gltf->nodes.size() + m_gltf->materials.size()); // Rough estimate

		m_pbrTemplate = Engine::assets().get<Graphics::MaterialTemplate>("default/PBR_template");
		m_pbrDefaultMat = Engine::assets().get<Graphics::MaterialInstance>("default/PBR_instance");

		size_t startScene = m_gltf->startScene.value_or(0);
		std::string sceneName = m_gltf->scenes[startScene].name.value_or(path.stem().string());
		m_rootEntity = scene.createEntity(sceneName);

		loadScene(scene, startScene);
	}

	void GLTFLoader::loadScene(Scene& scene, size_t sceneIndex)
	{
		auto& sceneNode = m_gltf->scenes[sceneIndex];

		// Correct coordinate system (GLTF uses Y-up, Z-forward)
		m_rootEntity.get<Transform>().rotation = glm::radians(glm::vec3{ 90.0f, 0.0f, 0.0f });

		struct Node
		{
			size_t index;
			Entity parent;
		};

		std::vector<Node> nodeStack;
		nodeStack.reserve(m_gltf->nodes.size());
		for (auto& nodeIndex : sceneNode.nodes)
			nodeStack.emplace_back(Node{ nodeIndex, m_rootEntity });

		uint32_t nodeCounter = 0;
		while (!nodeStack.empty())
		{
			auto& [nodeIndex, parent] = nodeStack.back();
			auto& node = m_gltf->nodes[nodeIndex];

			// Create entity for node
			auto nodeEntity = scene.createEntity(node.name.value_or("Node" + std::to_string(nodeCounter++)));
			nodeEntity.get<Transform>() = toTransform(node.transform);
			nodeEntity.setParent(parent);

			nodeStack.pop_back();

			for (auto& childIndex : node.children)
				nodeStack.emplace_back(Node{ childIndex, nodeEntity });

			// Mesh
			if (!node.mesh.has_value())
				continue;

			size_t meshIndex = node.mesh.value();
			auto& mesh = m_gltf->meshes[meshIndex];
			m_meshes[meshIndex].resize(mesh.primitives.size());
			for (size_t i = 0; i < mesh.primitives.size(); i++)
			{
				auto& primitive = mesh.primitives[i];
				auto matInstance = primitive.material ? loadMaterial(*primitive.material) : m_pbrDefaultMat;

				auto meshEntity = scene.createEntity(mesh.name.value_or("Mesh") + std::to_string(i));
				meshEntity.add<Material>(matInstance);
				meshEntity.add<Mesh>(loadMesh(meshIndex, i));
				meshEntity.setParent(nodeEntity);
			}
		}
	}

	auto GLTFLoader::toTransform(const GLTF::Node::Transform& nodeTransform) -> Transform
	{
		Transform result{};
		std::visit([&](auto&& val)
			{
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, GLTF::Mat4>)
				{
					glm::mat4 matrix = glm::make_mat4(val.data());
					Math::decomposeTRS(matrix, result.location, result.rotation, result.scale);
				}
				else if constexpr (std::is_same_v<T, GLTF::Node::TRS>)
				{
					result.location = { val.translation[0], val.translation[1], val.translation[2] };
					result.rotation = { val.rotation[3], val.rotation[0], val.rotation[1], val.rotation[2] };
					result.scale = { val.scale[0], val.scale[1], val.scale[2] };
				}
			}, nodeTransform);
		return result;
	}

	auto GLTFLoader::loadMesh(size_t meshIndex, size_t primitiveIndex) -> std::shared_ptr<Graphics::StaticMesh>
	{
		AGX_ASSERT_X(meshIndex < m_meshes.size(), "Mesh index is out of range");
		AGX_ASSERT_X(primitiveIndex < m_gltf->meshes[meshIndex].primitives.size(), "Primitive index is out of range");

		// Return cached mesh
		if (m_meshes[meshIndex][primitiveIndex])
			return m_meshes[meshIndex][primitiveIndex];

		// Load mesh
		auto& primitive = m_gltf->meshes[meshIndex].primitives[primitiveIndex];

		Graphics::MeshPreprocessor::Input input{};
		GLTF::copyAttribute("POSITION", input.positions, primitive, *m_gltf);
		GLTF::copyAttribute("COLOR_0", input.colors, primitive, *m_gltf);
		GLTF::copyAttribute("NORMAL", input.normals, primitive, *m_gltf);
		GLTF::copyAttribute("TEXCOORD_0", input.uvs, primitive, *m_gltf);
		GLTF::copyIndices(input.indices, primitive, *m_gltf);

		auto info = Graphics::MeshPreprocessor::process(input);
		auto mesh = std::make_shared<Graphics::StaticMesh>(info);
		m_meshes[meshIndex].emplace_back(mesh);
		return mesh;
	}

	auto GLTFLoader::loadMaterial(size_t materialIndex) -> std::shared_ptr<Graphics::MaterialInstance>
	{
		AGX_ASSERT_X(materialIndex < m_gltf->materials.size(), "Material index is out of range");

		// Return cached material
		if (m_materials[materialIndex])
			return m_materials[materialIndex];

		auto materialInstance = Graphics::MaterialInstance::create(m_pbrTemplate);

		auto& material = m_gltf->materials[materialIndex];
		if (auto& pbr = material.pbrMetallicRoughness)
		{
			if (auto& baseColor = pbr->baseColorTexture)
				materialInstance->setParameter("albedoMap", loadTexture(baseColor->index, VK_FORMAT_R8G8B8A8_SRGB));

			if (auto& metallicRoughnes = pbr->metallicRoughnessTexture)
				materialInstance->setParameter("metalRoughnessMap", loadTexture(metallicRoughnes->index, VK_FORMAT_R8G8B8A8_UNORM));

			materialInstance->setParameter("albedo", glm::make_vec3(pbr->baseColorFactor.data()));
			materialInstance->setParameter("metallic", pbr->metallicFactor);
			materialInstance->setParameter("roughness", pbr->roughnessFactor);
		}

		if (auto& normal = material.normalTexture)
			materialInstance->setParameter("normalMap", loadTexture(normal->index, VK_FORMAT_R8G8B8A8_UNORM));

		if (auto& occlusion = material.occlusionTexture)
			materialInstance->setParameter("ambientOcclusionMap", loadTexture(occlusion->index, VK_FORMAT_R8G8B8A8_UNORM));

		if (auto& emissive = material.emissiveTexture)
			materialInstance->setParameter("emissiveMap", loadTexture(emissive->index, VK_FORMAT_R8G8B8A8_SRGB));

		materialInstance->setParameter("emissive", glm::make_vec3(material.emissiveFactor.data()));

		m_materials[materialIndex] = materialInstance;
		return materialInstance;
	}

	auto GLTFLoader::loadTexture(size_t textureIndex, VkFormat format) -> std::shared_ptr<Graphics::Texture>
	{
		AGX_ASSERT_X(textureIndex < m_gltf->textures.size(), "Texture index is out of range");

		// Return cached texture
		if (m_textures[textureIndex])
			return m_textures[textureIndex];

		// Load texture
		auto& source = m_gltf->textures[textureIndex].source;
		AGX_ASSERT_X(source.has_value(), "Texture source is required");

		auto& image = m_gltf->images[source.value()];
		std::shared_ptr<Graphics::Texture> texture;
		std::visit([&](auto&& val)
			{
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, GLTF::Image::UriData>)
				{
					texture = Graphics::Texture::loadFromFile(m_gltf->basePath / val.uri, format);
				}
				else if constexpr (std::is_same_v<T, GLTF::Image::BufferViewData>)
				{
					AGX_ASSERT_X(false, "BufferView image data not supported yet");
				}
			}, image.data);

		m_textures[textureIndex] = texture;
		return texture;
	}
}
