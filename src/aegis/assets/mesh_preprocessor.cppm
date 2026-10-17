export module aegis.assets:mesh_preprocessor;
import std;
import :error;
import :mesh_data;
import aegis.math;

export namespace aegis::assets
{
/// @brief Turns raw vertex streams into an optimized, meshletized MeshData. CPU only.
class MeshPreprocessor
{
public:
    struct Input
    {
        std::vector<glm::vec3> positions;   // Required
        std::vector<glm::vec3> normals;     // Required, one per position
        std::vector<glm::vec2> uvs;         // Optional; missing entries become (0, 0)
        std::vector<glm::vec3> colors;      // Optional; missing entries become white
        std::vector<std::uint32_t> indices; // Optional; empty means one triangle per 3 vertices

        float overdrawThreshold{ 1.05f };
        std::size_t maxVerticesPerMeshlet{ 64 };
        std::size_t maxTrianglesPerMeshlet{ 126 };
        float coneWeight{ 0.0f };
    };

    /// @brief Deduplicates vertices, optimizes for vertex cache / overdraw / fetch, computes bounds,
    ///        and builds meshlets.
    /// @return ErrorCode::InvalidData for missing positions, mismatched normals, or a vertex or
    ///         index count that is not a whole number of triangles.
    [[nodiscard]] static auto process(Input input) -> std::expected<MeshData, Error>;
};
}
