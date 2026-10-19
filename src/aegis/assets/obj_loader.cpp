module;
#include <tiny_obj_loader.h>

module aegis.assets;
import std;
import aegis.rhi;
import aegis.math;
import aegis.scene.registry;

namespace aegis::assets
{
auto Loader::loadObj(const Desc& desc, const std::filesystem::path& path) -> std::expected<aegis::scene::Entity, Error>
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, path.string().c_str()))
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to load OBJ '{}': {}", path.string(), error));

    // Unindexed: one vertex per face corner. MeshPreprocessor deduplicates them.
    MeshPreprocessor::Input input;
    for (const auto& shape : shapes)
    {
        for (const auto& index : shape.mesh.indices)
        {
            const auto vertex = static_cast<std::size_t>(index.vertex_index);
            input.positions.emplace_back(attrib.vertices[3 * vertex + 0], attrib.vertices[3 * vertex + 1],
                attrib.vertices[3 * vertex + 2]);
            input.colors.emplace_back(3 * vertex + 2 < attrib.colors.size()
                                          ? glm::vec3{ attrib.colors[3 * vertex + 0], attrib.colors[3 * vertex + 1],
                                                attrib.colors[3 * vertex + 2] }
                                          : glm::vec3{ 1.0f });

            if (index.normal_index < 0)
                return makeError(ErrorCode::InvalidData, std::format("OBJ '{}' has a vertex without a normal", path.string()));
            const auto normal = static_cast<std::size_t>(index.normal_index);
            input.normals.emplace_back(attrib.normals[3 * normal + 0], attrib.normals[3 * normal + 1],
                attrib.normals[3 * normal + 2]);

            // OBJ's v runs up from the image's bottom row; the engine's (like glTF's) runs down from the top.
            const auto uv = index.texcoord_index;
            input.uvs.emplace_back(uv < 0 ? glm::vec2{ 0.0f }
                                          : glm::vec2{ attrib.texcoords[2 * static_cast<std::size_t>(uv) + 0],
                                                1.0f - attrib.texcoords[2 * static_cast<std::size_t>(uv) + 1] });
        }
    }

    auto data = MeshPreprocessor::process(std::move(input));
    if (!data)
    {
        auto error = data.error();
        error.message = std::format("OBJ '{}': {}", path.string(), error.message);
        return std::unexpected{ std::move(error) };
    }

    const auto name = path.stem().string();
    auto mesh = StaticMesh::create(desc.device, *data, name);
    if (!mesh)
        return std::unexpected{ mesh.error() };
    (*mesh)->setPath(path);

    const auto root = desc.scene.create(name);
    desc.scene.add<Mesh>(root, std::move(*mesh));
    desc.scene.add<Material>(root, desc.defaultMaterial);
    return root;
}
}
