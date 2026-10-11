module;
#include <meshoptimizer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <utility>
#include <vector>

module aegis.assets;
import Aegis.Math;

namespace aegis::assets
{
namespace
{
auto interleave(const MeshPreprocessor::Input& input) -> std::vector<Vertex>
{
    std::vector<Vertex> vertices(input.positions.size());
    for (std::size_t i = 0; i < input.positions.size(); ++i)
    {
        vertices[i].position = input.positions[i];
        vertices[i].normal = input.normals[i];
        vertices[i].uv = i < input.uvs.size() ? input.uvs[i] : glm::vec2{ 0.0f };
        vertices[i].color = i < input.colors.size() ? input.colors[i] : glm::vec3{ 1.0f };
    }
    return vertices;
}

auto validate(const MeshPreprocessor::Input& input) -> std::expected<void, Error>
{
    if (input.positions.empty())
        return makeError(ErrorCode::InvalidData, "Mesh has no positions");
    if (input.normals.size() != input.positions.size())
    {
        return makeError(ErrorCode::InvalidData,
            std::format("Mesh has {} normals for {} positions", input.normals.size(), input.positions.size()));
    }

    const auto indexCount = input.indices.empty() ? input.positions.size() : input.indices.size();
    if (indexCount % 3 != 0)
        return makeError(ErrorCode::InvalidData, std::format("Mesh index count {} is not a multiple of 3", indexCount));

    const auto outOfRange = std::ranges::find_if(input.indices,
        [count = input.positions.size()](std::uint32_t index) { return index >= count; });
    if (outOfRange != input.indices.end())
    {
        return makeError(ErrorCode::InvalidData,
            std::format("Mesh index {} is out of range for {} vertices", *outOfRange, input.positions.size()));
    }
    return {};
}
}

auto MeshPreprocessor::process(Input input) -> std::expected<MeshData, Error>
{
    if (auto valid = validate(input); !valid)
        return std::unexpected{ valid.error() };

    auto vertices = interleave(input);
    const auto initialVertexCount = vertices.size();
    const bool hasIndices = !input.indices.empty();
    const auto indexCount = hasIndices ? input.indices.size() : initialVertexCount;

    // Vertex deduplication: remap, then rewrite both streams through the remap table.

    std::vector<std::uint32_t> remap(initialVertexCount);
    const auto vertexCount = meshopt_generateVertexRemap(remap.data(),
        hasIndices ? input.indices.data() : nullptr, indexCount, vertices.data(), initialVertexCount,
        sizeof(Vertex));

    std::vector<std::uint32_t> indices(indexCount);
    meshopt_remapIndexBuffer(indices.data(), hasIndices ? input.indices.data() : nullptr, indexCount,
        remap.data());
    meshopt_remapVertexBuffer(vertices.data(), vertices.data(), initialVertexCount, sizeof(Vertex), remap.data());
    vertices.resize(vertexCount);

    // Optimizations

    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
    meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), &vertices[0].position.x,
        vertices.size(), sizeof(Vertex), input.overdrawThreshold);
    meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(),
        sizeof(Vertex));

    // Bounds

    const auto sphere = meshopt_computeSphereBounds(&vertices[0].position.x, vertices.size(), sizeof(Vertex),
        nullptr, 0);
    const BoundingSphere bounds{
        .center = { sphere.center[0], sphere.center[1], sphere.center[2] },
        .radius = sphere.radius,
    };

    // Meshlets, sized by meshoptimizer's documented worst case

    const auto maxMeshlets = meshopt_buildMeshletsBound(indices.size(), input.maxVerticesPerMeshlet,
        input.maxTrianglesPerMeshlet);
    std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
    std::vector<std::uint32_t> meshletVertices(maxMeshlets * input.maxVerticesPerMeshlet);
    std::vector<std::uint8_t> meshletPrimitives(maxMeshlets * input.maxTrianglesPerMeshlet * 3);

    const auto meshletCount = meshopt_buildMeshlets(meshoptMeshlets.data(), meshletVertices.data(),
        meshletPrimitives.data(), indices.data(), indices.size(), &vertices[0].position.x, vertices.size(),
        sizeof(Vertex), input.maxVerticesPerMeshlet, input.maxTrianglesPerMeshlet, input.coneWeight);
    if (meshletCount == 0)
        return makeError(ErrorCode::InvalidData, "Mesh produced no meshlets");

    const auto& last = meshoptMeshlets[meshletCount - 1];
    meshoptMeshlets.resize(meshletCount);
    meshletVertices.resize(last.vertex_offset + last.vertex_count);
    meshletPrimitives.resize(last.triangle_offset + last.triangle_count * 3);

    std::vector<Meshlet> meshlets;
    meshlets.reserve(meshletCount);
    for (const auto& meshlet : meshoptMeshlets)
    {
        meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletPrimitives[meshlet.triangle_offset],
            meshlet.triangle_count, meshlet.vertex_count);

        const auto cone = meshopt_computeMeshletBounds(&meshletVertices[meshlet.vertex_offset],
            &meshletPrimitives[meshlet.triangle_offset], meshlet.triangle_count, &vertices[0].position.x,
            vertices.size(), sizeof(Vertex));

        meshlets.emplace_back(Meshlet{
            .bounds = { glm::vec3{ cone.center[0], cone.center[1], cone.center[2] }, cone.radius },
            .coneAxis = { cone.cone_axis_s8[0], cone.cone_axis_s8[1], cone.cone_axis_s8[2] },
            .coneCutoff = cone.cone_cutoff_s8,
            .vertexOffset = meshlet.vertex_offset,
            .primitiveOffset = meshlet.triangle_offset,
            .vertexCount = static_cast<std::uint8_t>(meshlet.vertex_count),
            .primitiveCount = static_cast<std::uint8_t>(meshlet.triangle_count),
        });
    }

    return MeshData{
        .vertices = std::move(vertices),
        .indices = std::move(indices),
        .meshlets = std::move(meshlets),
        .meshletVertices = std::move(meshletVertices),
        .meshletPrimitives = std::move(meshletPrimitives),
        .bounds = bounds,
    };
}
}
