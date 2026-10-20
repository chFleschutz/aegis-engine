module;
#include <meshoptimizer.h>

module aegis.assets;
import std;
import aegis.math;

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
    // The mesh shader's output arrays hold meshletMaxVertices / meshletMaxPrimitives, and meshoptimizer
    // needs room for one triangle. Larger meshlets would be undefined behavior on the GPU.
    if (input.maxVerticesPerMeshlet < 3 || input.maxVerticesPerMeshlet > meshletMaxVertices)
    {
        return makeError(ErrorCode::InvalidData, std::format("Meshlet vertex limit {} is outside [3, {}]",
                                                     input.maxVerticesPerMeshlet, meshletMaxVertices));
    }
    if (input.maxTrianglesPerMeshlet < 1 || input.maxTrianglesPerMeshlet > meshletMaxPrimitives)
    {
        return makeError(ErrorCode::InvalidData, std::format("Meshlet triangle limit {} is outside [1, {}]",
                                                     input.maxTrianglesPerMeshlet, meshletMaxPrimitives));
    }

    if (input.positions.empty())
        return makeError(ErrorCode::InvalidData, "Mesh has no positions");
    if (!input.normals.empty() && input.normals.size() != input.positions.size())
    {
        return makeError(ErrorCode::InvalidData,
            std::format("Mesh has {} normals for {} positions", input.normals.size(), input.positions.size()));
    }

    const auto indexCount = input.indices.empty() ? input.positions.size() : input.indices.size();
    if (input.topology == MeshPreprocessor::Topology::TriangleList && indexCount % 3 != 0)
        return makeError(ErrorCode::InvalidData, std::format("Mesh index count {} is not a multiple of 3", indexCount));
    if (input.topology != MeshPreprocessor::Topology::TriangleList && indexCount < 3)
        return makeError(ErrorCode::InvalidData, std::format("Mesh strip or fan has {} indices, fewer than 3", indexCount));

    const auto outOfRange = std::ranges::find_if(input.indices,
        [count = input.positions.size()](std::uint32_t index) { return index >= count; });
    if (outOfRange != input.indices.end())
    {
        return makeError(ErrorCode::InvalidData,
            std::format("Mesh index {} is out of range for {} vertices", *outOfRange, input.positions.size()));
    }
    return {};
}

// Expands a strip or fan of at least 3 indices into a list, with glTF's winding: strip triangle i
// is {v[i], v[i + 1 + i % 2], v[i + 2 - i % 2]}, fan triangle i is {v[i + 1], v[i + 2], v[0]}.
auto toTriangleList(const MeshPreprocessor::Input& input) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> sequential;
    if (input.indices.empty())
    {
        sequential.resize(input.positions.size());
        std::ranges::iota(sequential, 0u);
    }
    const auto& v = input.indices.empty() ? sequential : input.indices;

    const auto triangleCount = v.size() - 2;
    std::vector<std::uint32_t> list;
    list.reserve(3 * triangleCount);
    for (std::size_t i = 0; i < triangleCount; ++i)
    {
        if (input.topology == MeshPreprocessor::Topology::TriangleStrip)
            list.insert(list.end(), { v[i], v[i + 1 + i % 2], v[i + 2 - i % 2] });
        else
            list.insert(list.end(), { v[i + 1], v[i + 2], v[0] });
    }
    return list;
}

// Replaces the vertices by three per triangle, each with its triangle's normal (the glTF fallback for a
// missing NORMAL). The input must be a triangle list. A degenerate triangle gets +Z.
auto generateFlatNormals(MeshPreprocessor::Input& input) -> void
{
    const auto cornerCount = input.indices.empty() ? input.positions.size() : input.indices.size();
    const auto source = [&](std::size_t corner) { return input.indices.empty() ? corner : input.indices[corner]; };

    MeshPreprocessor::Input expanded;
    expanded.positions.reserve(cornerCount);
    expanded.normals.reserve(cornerCount);
    for (std::size_t corner = 0; corner < cornerCount; ++corner)
    {
        const auto index = source(corner);
        expanded.positions.push_back(input.positions[index]);
        if (index < input.uvs.size())
            expanded.uvs.push_back(input.uvs[index]);
        else if (!input.uvs.empty())
            expanded.uvs.emplace_back(0.0f);
        if (index < input.colors.size())
            expanded.colors.push_back(input.colors[index]);
        else if (!input.colors.empty())
            expanded.colors.emplace_back(1.0f);
    }
    for (std::size_t corner = 0; corner < cornerCount; corner += 3)
    {
        const auto& a = expanded.positions[corner];
        const auto cross = glm::cross(expanded.positions[corner + 1] - a, expanded.positions[corner + 2] - a);
        const auto length = glm::length(cross);
        // Adding 0 turns -0 into +0, so equal normals are bitwise equal and the vertices merge.
        const auto normal = (length > 1e-20f ? cross / length : glm::vec3{ 0.0f, 0.0f, 1.0f }) + glm::vec3{ 0.0f };
        expanded.normals.insert(expanded.normals.end(), 3, normal);
    }

    input.positions = std::move(expanded.positions);
    input.normals = std::move(expanded.normals);
    input.uvs = std::move(expanded.uvs);
    input.colors = std::move(expanded.colors);
    input.indices.clear();
}
}

auto MeshPreprocessor::process(Input input) -> std::expected<MeshData, Error>
{
    if (auto valid = validate(input); !valid)
        return std::unexpected{ valid.error() };

    if (input.topology != Topology::TriangleList)
    {
        input.indices = toTriangleList(input);
        input.topology = Topology::TriangleList;
    }

    if (input.normals.empty())
        generateFlatNormals(input);

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
