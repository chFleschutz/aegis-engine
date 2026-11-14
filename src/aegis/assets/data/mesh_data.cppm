export module aegis.assets:mesh_data;
import std;
export import :gpu_types; // Vertex, Meshlet, BoundingSphere
import aegis.math;

export namespace aegis::assets
{
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
