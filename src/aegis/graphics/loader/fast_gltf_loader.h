#pragma once

#include "scene/scene.h"
#include "graphics/resources/static_mesh.h"
#include "graphics/resources/texture.h"
#include "graphics/material/material_template.h"
#include "graphics/material/material_instance.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

namespace Aegis::Scene
{
	class FastGLTFLoader
	{
	public:
		FastGLTFLoader(Scene& scene, const std::filesystem::path& path);

		[[nodiscard]] auto rootEntity() const -> Entity { return m_rootEntity; }

	private:
		inline static fastgltf::Parser parser;

		void loadMeshes(const fastgltf::Asset& gltf);
		void loadTextures(const fastgltf::Asset& gltf);
		void loadMaterials(const fastgltf::Asset& gltf);
		void buildScene(Scene& scene, const fastgltf::Asset& gltf, size_t sceneIndex);

		auto queryMaterial(const fastgltf::Mesh& mesh, size_t subIdx) -> std::shared_ptr<Graphics::MaterialInstance>;

		Entity m_rootEntity;
		std::shared_ptr<Graphics::MaterialTemplate> m_pbrTemplate;
		std::shared_ptr<Graphics::MaterialInstance> m_pbrDefaultMat;
		std::filesystem::path m_basePath;
		std::vector<VkFormat> m_textureFormats;
		std::vector<std::shared_ptr<Graphics::Texture>> m_textureCache;
		std::vector<std::shared_ptr<Graphics::MaterialInstance>> m_materialCache;
		std::vector<std::vector<std::shared_ptr<Graphics::StaticMesh>>> m_meshCache;
	};
}