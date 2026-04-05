#pragma once

#include "graphics/resources/static_mesh.h"
#include "graphics/resources/texture.h"
#include "scene/components.h"
#include "scene/entity.h"

#include <gltf.h>

namespace Aegis::Scene
{
	class GLTFLoader
	{
	public:
		GLTFLoader(Scene& scene, const std::filesystem::path& path);

		[[nodiscard]] auto rootEntity() const -> Entity { return m_rootEntity; }

	private:
		void loadScene(Scene& scene, size_t sceneIndex);
		auto toTransform(const GLTF::Node::Transform& nodeTransform) -> Transform;
		auto loadMesh(size_t meshIndex, size_t primitiveIndex) -> std::shared_ptr<Graphics::StaticMesh>;
		auto loadMaterial(size_t materialIndex) -> std::shared_ptr<Graphics::MaterialInstance>;
		auto loadTexture(size_t textureIndex, VkFormat format) -> std::shared_ptr<Graphics::Texture>;

		std::optional<GLTF::GLTF> m_gltf;
		std::vector<std::shared_ptr<Graphics::Texture>> m_textures;
		std::vector<std::shared_ptr<Graphics::MaterialInstance>> m_materials;
		std::vector<std::vector<std::shared_ptr<Graphics::StaticMesh>>> m_meshes;
		std::vector<Entity> m_entities;
		Entity m_rootEntity;

		std::shared_ptr<Graphics::MaterialTemplate> m_pbrTemplate;
		std::shared_ptr<Graphics::MaterialInstance> m_pbrDefaultMat;
	};
}
