export module aegis.assets:mesh_preprocessor;
import std;
import :error;
import :gpu_types;
import :mesh_data;
import aegis.math;

export namespace aegis::assets
{
/// @brief Turns raw vertex streams into an optimized, meshletized MeshData. CPU only.
class MeshPreprocessor
{
public:
    /// @brief How the indices form triangles. Strips and fans are converted to lists, with the
    ///        winding glTF specifies.
    enum class Topology
    {
        TriangleList,
        TriangleStrip,
        TriangleFan,
    };

    struct Input
    {
        std::vector<glm::vec3> positions;   // Required
        std::vector<glm::vec3> normals;     // Optional, one per position; missing normals are generated flat (per triangle)
        std::vector<glm::vec2> uvs;         // Optional; missing entries become (0, 0)
        std::vector<glm::vec3> colors;      // Optional; missing entries become white
        std::vector<std::uint32_t> indices; // Optional; empty means the vertices in order
        Topology topology{ Topology::TriangleList };

        float overdrawThreshold{ 1.05f };
        /// Meshlet limits, at most meshletMaxVertices and meshletMaxPrimitives (the mesh shader's output
        /// arrays); process rejects larger ones, and fewer than 3 vertices or 1 triangle.
        std::size_t maxVerticesPerMeshlet{ meshletMaxVertices };
        std::size_t maxTrianglesPerMeshlet{ meshletMaxPrimitives };
        float coneWeight{ 0.0f };
    };

    /// @brief Deduplicates vertices, optimizes for vertex cache / overdraw / fetch, computes bounds,
    ///        and builds meshlets.
    /// @return ErrorCode::InvalidData for missing positions, a normal count that is neither 0 nor the position count, an out-of-range
    ///         index, a vertex or index count that is not a whole number of triangles (a list
    ///         count not a multiple of 3, a strip or fan with fewer than 3), or meshlet limits the
    ///         mesh shader cannot hold.
    [[nodiscard]] static auto process(Input input) -> std::expected<MeshData, Error>;
};
}
