module aegis.assets;
import std;

namespace aegis::assets
{
namespace
{
static_assert(std::endian::native == std::endian::little, "Mesh blobs are little-endian");

auto alignUp(std::size_t offset) -> std::size_t
{
    return (offset + meshBlobAlignment - 1) / meshBlobAlignment * meshBlobAlignment;
}

template<class T>
auto copyIn(std::vector<std::byte>& blob, std::size_t offset, const std::vector<T>& values) -> void
{
    if (!values.empty())
        std::memcpy(blob.data() + offset, values.data(), values.size() * sizeof(T));
}

template<class T>
auto copyOut(std::span<const std::byte> blob, std::size_t offset, std::size_t count) -> std::vector<T>
{
    std::vector<T> values(count);
    if (count > 0)
        std::memcpy(values.data(), blob.data() + offset, count * sizeof(T));
    return values;
}
}

auto meshBlobLayout(const MeshBlobHeader& header) -> MeshBlobLayout
{
    MeshBlobLayout layout;
    auto offset = alignUp(sizeof(MeshBlobHeader));
    const auto place = [&offset](std::size_t bytes) {
        const auto start = offset;
        offset = alignUp(offset + bytes);
        return start;
    };
    layout.vertices = place(std::size_t{ header.vertexCount } * sizeof(Vertex));
    layout.indices = place(std::size_t{ header.indexCount } * sizeof(std::uint32_t));
    layout.meshlets = place(std::size_t{ header.meshletCount } * sizeof(Meshlet));
    layout.meshletVertices = place(std::size_t{ header.meshletVertexCount } * sizeof(std::uint32_t));
    layout.meshletPrimitives = place(header.meshletPrimitiveBytes);
    layout.size = offset;
    return layout;
}

auto writeMeshBlob(const MeshData& mesh, std::uint64_t key) -> std::vector<std::byte>
{
    const MeshBlobHeader header{
        .key = key,
        .vertexCount = static_cast<std::uint32_t>(mesh.vertices.size()),
        .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
        .meshletCount = static_cast<std::uint32_t>(mesh.meshlets.size()),
        .meshletVertexCount = static_cast<std::uint32_t>(mesh.meshletVertices.size()),
        .meshletPrimitiveBytes = static_cast<std::uint32_t>(mesh.meshletPrimitives.size()),
        .bounds = mesh.bounds,
    };
    const auto layout = meshBlobLayout(header);
    std::vector<std::byte> blob(layout.size); // Zeroed, padding included, so equal meshes give equal bytes
    std::memcpy(blob.data(), &header, sizeof(header));
    copyIn(blob, layout.vertices, mesh.vertices);
    copyIn(blob, layout.indices, mesh.indices);
    copyIn(blob, layout.meshlets, mesh.meshlets);
    copyIn(blob, layout.meshletVertices, mesh.meshletVertices);
    copyIn(blob, layout.meshletPrimitives, mesh.meshletPrimitives);
    return blob;
}

auto readMeshBlobHeader(std::span<const std::byte> blob) -> std::expected<MeshBlobHeader, Error>
{
    MeshBlobHeader header;
    if (blob.size() < sizeof(header))
        return makeError(ErrorCode::DecodeFailed, std::format("A mesh blob of {} bytes is shorter than its header", blob.size()));
    std::memcpy(&header, blob.data(), sizeof(header));
    if (header.magic != meshBlobMagic || header.type != meshBlobTypeStaticMesh)
        return makeError(ErrorCode::DecodeFailed, "Not a mesh blob");
    if (header.version != meshBlobVersion)
    {
        return makeError(ErrorCode::UnsupportedFormat,
            std::format("Mesh blob version {} is not the supported version {}", header.version, meshBlobVersion));
    }
    if (const auto size = meshBlobLayout(header).size; size != blob.size())
        return makeError(ErrorCode::InvalidData, std::format("A mesh blob of {} bytes has counts for {} bytes", blob.size(), size));
    return header;
}

auto readMeshBlob(std::span<const std::byte> blob) -> std::expected<MeshData, Error>
{
    const auto header = readMeshBlobHeader(blob);
    if (!header)
        return std::unexpected{ header.error() };
    const auto layout = meshBlobLayout(*header);
    return MeshData{
        .vertices = copyOut<Vertex>(blob, layout.vertices, header->vertexCount),
        .indices = copyOut<std::uint32_t>(blob, layout.indices, header->indexCount),
        .meshlets = copyOut<Meshlet>(blob, layout.meshlets, header->meshletCount),
        .meshletVertices = copyOut<std::uint32_t>(blob, layout.meshletVertices, header->meshletVertexCount),
        .meshletPrimitives = copyOut<std::uint8_t>(blob, layout.meshletPrimitives, header->meshletPrimitiveBytes),
        .bounds = header->bounds,
    };
}
}
