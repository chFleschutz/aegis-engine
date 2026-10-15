module;
#include <cstdint>
#include <vector>

export module aegis.assets:mesh_data;
import aegis.math;

export namespace aegis::assets
{
/// @brief Interleaved vertex. Shaders read it through a buffer device address (no vertex input).
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 color;
};

struct BoundingSphere
{
    glm::vec3 center{ 0.0f };
    float radius{ 0.0f };
};

/// @brief A cluster of up to 64 vertices / 126 triangles, as meshoptimizer builds them.
struct Meshlet
{
    BoundingSphere bounds;
    std::int8_t coneAxis[3];
    std::int8_t coneCutoff;
    std::uint32_t vertexOffset;    // Into MeshData::meshletVertices
    std::uint32_t primitiveOffset; // Into MeshData::meshletPrimitives, in bytes (3 per triangle)
    std::uint8_t vertexCount;
    std::uint8_t primitiveCount;
};

/// @brief A mesh ready for upload: what MeshPreprocessor produces and StaticMesh consumes.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Meshlet> meshlets;
    std::vector<std::uint32_t> meshletVertices;  // Indices into 'vertices'
    std::vector<std::uint8_t> meshletPrimitives; // Indices into each meshlet's vertex list, 3 per triangle
    BoundingSphere bounds;
};
}
