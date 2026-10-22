export module aegis.assets:mesh_blob;
import std;
import :error;
import :gpu_types;
import :mesh_data;

// The cooked form of a StaticMesh, as the import cache keeps it and export packs it: a fixed header, then the
// arrays of MeshData exactly as StaticMesh uploads them, each starting at a multiple of meshBlobAlignment from
// the blob's start. Little-endian, uncompressed (compression comes later; a new version then marks it).

export namespace aegis::assets
{
/// @brief "AEGM" at the start of every mesh blob.
inline constexpr std::array<char, 4> meshBlobMagic{ 'A', 'E', 'G', 'M' };
/// @brief MeshBlobHeader::type of a StaticMesh.
inline constexpr std::uint32_t meshBlobTypeStaticMesh{ 1 };
/// @brief Bumped on every change to the layout or to Vertex / Meshlet; a blob of another version is not read.
inline constexpr std::uint32_t meshBlobVersion{ 1 };
/// @brief Every array starts at a multiple of this from the blob's start.
inline constexpr std::size_t meshBlobAlignment{ 16 };

struct MeshBlobHeader
{
    std::array<char, 4> magic{ meshBlobMagic };
    std::uint32_t type{ meshBlobTypeStaticMesh };
    std::uint32_t version{ meshBlobVersion };
    std::uint32_t reserved{ 0 };
    std::uint64_t key{ 0 }; // The import cache key of the source it was made from
    std::uint32_t vertexCount{ 0 };
    std::uint32_t indexCount{ 0 };
    std::uint32_t meshletCount{ 0 };
    std::uint32_t meshletVertexCount{ 0 };
    std::uint32_t meshletPrimitiveBytes{ 0 }; // 3 per triangle
    std::uint32_t reserved2{ 0 };
    BoundingSphere bounds;
};
static_assert(sizeof(MeshBlobHeader) == 64);

/// @brief Where each array of a mesh blob starts, in bytes from the blob's start, and the blob's size.
struct MeshBlobLayout
{
    std::size_t vertices{ 0 };
    std::size_t indices{ 0 };
    std::size_t meshlets{ 0 };
    std::size_t meshletVertices{ 0 };
    std::size_t meshletPrimitives{ 0 };
    std::size_t size{ 0 };
};

/// @brief The layout the counts of 'header' give: the arrays in the order of MeshData, each aligned.
[[nodiscard]] auto meshBlobLayout(const MeshBlobHeader& header) -> MeshBlobLayout;

/// @brief 'mesh' as a blob made from the source with import cache key 'key'.
[[nodiscard]] auto writeMeshBlob(const MeshData& mesh, std::uint64_t key) -> std::vector<std::byte>;

/// @brief The header of a mesh blob, checked: magic, type and version, and a size that holds every array.
/// @return ErrorCode::DecodeFailed for a short blob or another magic or type, ErrorCode::UnsupportedFormat for
///         another version, ErrorCode::InvalidData for a size that does not match the counts.
[[nodiscard]] auto readMeshBlobHeader(std::span<const std::byte> blob) -> std::expected<MeshBlobHeader, Error>;

/// @brief The mesh a blob holds. The errors of readMeshBlobHeader.
[[nodiscard]] auto readMeshBlob(std::span<const std::byte> blob) -> std::expected<MeshData, Error>;
}
