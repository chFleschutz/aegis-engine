module;
#include <tiny_obj_loader.h>

module aegis.assets;
import std;
import aegis.core;
import aegis.math;

namespace aegis::assets
{
auto importObj(std::istream& stream, std::string_view name) -> std::expected<ModelData, Error>
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;
    // No material reader: .mtl files are not read, every face gets the default material.
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, &stream))
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to load OBJ '{}': {}", name, error));

    // Unindexed: one vertex per face corner. MeshPreprocessor deduplicates them.
    MeshPreprocessor::Input input;
    bool missingNormals{ false };
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
            {
                missingNormals = true;
                input.normals.emplace_back(0.0f);
            }
            else
            {
                const auto normal = static_cast<std::size_t>(index.normal_index);
                input.normals.emplace_back(attrib.normals[3 * normal + 0], attrib.normals[3 * normal + 1],
                    attrib.normals[3 * normal + 2]);
            }

            // OBJ's v runs up from the image's bottom row; the engine's (like glTF's) runs down from the top.
            const auto uv = index.texcoord_index;
            input.uvs.emplace_back(uv < 0 ? glm::vec2{ 0.0f }
                                          : glm::vec2{ attrib.texcoords[2 * static_cast<std::size_t>(uv) + 0],
                                                1.0f - attrib.texcoords[2 * static_cast<std::size_t>(uv) + 1] });
        }
    }

    // A face corner without a normal: the whole mesh gets flat normals, as mixing would shade seams.
    if (missingNormals)
    {
        log::warn("OBJ '{}' has face corners without a normal; generating flat normals", name);
        input.normals.clear();
    }

    auto data = MeshPreprocessor::process(std::move(input));
    if (!data)
    {
        auto failure = data.error();
        failure.message = std::format("OBJ '{}': {}", name, failure.message);
        return std::unexpected{ std::move(failure) };
    }

    ModelData model{ .name = std::string{ name } };
    model.meshes.push_back(ModelMesh{ .name = std::string{ name }, .primitives = {} });
    model.meshes.back().primitives.push_back(ModelPrimitive{ .mesh = std::move(*data), .material = std::nullopt });
    model.rootMesh = 0;
    return model;
}
}
