#include "pch.h"
#include "obj_loader.h"

#include "graphics/resources/mesh_preprocessor.h"
#include "scene/components.h"
#include "engine.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace Aegis::Scene
{
	OBJLoader::OBJLoader(Scene& scene, const std::filesystem::path& path)
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		bool result = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str());
		AGX_ASSERT_X(result, "Failed to load OBJ file");

		size_t totalIndexCount = 0;
		for (const auto& shape : shapes) {
			totalIndexCount += shape.mesh.indices.size();
		}
	
		Graphics::MeshPreprocessor::Input raw{};
		raw.positions.reserve(totalIndexCount);
		raw.normals.reserve(totalIndexCount);
		raw.uvs.reserve(totalIndexCount);
		raw.colors.reserve(totalIndexCount);

		for (const auto& shape : shapes)
		{
			for (const auto& index : shape.mesh.indices)
			{
				glm::vec3 position{};
				glm::vec3 color{};
				glm::vec3 normal{};
				glm::vec2 uv{};

				if (index.vertex_index >= 0)
				{
					position.x = attrib.vertices[3 * index.vertex_index + 0];
					position.y = attrib.vertices[3 * index.vertex_index + 1];
					position.z = attrib.vertices[3 * index.vertex_index + 2];

					color.r = attrib.colors[3 * index.vertex_index + 0];
					color.g = attrib.colors[3 * index.vertex_index + 1];
					color.b = attrib.colors[3 * index.vertex_index + 2];
				}

				if (index.normal_index >= 0)
				{
					normal.x = attrib.normals[3 * index.normal_index + 0];
					normal.y = attrib.normals[3 * index.normal_index + 1];
					normal.z = attrib.normals[3 * index.normal_index + 2];
				}

				if (index.texcoord_index >= 0)
				{
					uv.x = attrib.texcoords[2 * index.texcoord_index + 0];
					uv.y = attrib.texcoords[2 * index.texcoord_index + 1];
				}

				raw.positions.emplace_back(position);
				raw.colors.emplace_back(color);
				raw.normals.emplace_back(normal);
				raw.uvs.emplace_back(uv);
			}
		}

		auto info = Graphics::MeshPreprocessor::process(raw);
		auto mesh = std::make_shared<Graphics::StaticMesh>(info);

		m_rootEntity = scene.createEntity(path.stem().string());
		m_rootEntity.add<Mesh>(mesh);
		m_rootEntity.add<Material>(Engine::assets().get<Graphics::MaterialInstance>("default/PBR_instance"));
	}
}
