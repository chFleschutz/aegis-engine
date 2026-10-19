module aegis.assets;
import std;
import :upload;
import aegis.base;
import aegis.rhi;

namespace aegis::assets
{
namespace
{
struct BufferSource
{
    std::string_view label;
    std::span<const std::byte> bytes;
    rhi::BufferUsage extraUsage{ rhi::BufferUsage::None };
};
}

auto StaticMesh::create(rhi::Device& device, const MeshData& data, std::string_view name)
    -> std::expected<std::shared_ptr<StaticMesh>, Error>
{
    if (data.vertices.empty() || data.indices.empty() || data.meshlets.empty())
        return makeError(ErrorCode::InvalidData, std::format("Mesh '{}' has no vertices, indices or meshlets", name));

    std::array<BufferSource, BufferCount> sources{
        BufferSource{ "Vertices", std::as_bytes(std::span{ data.vertices }) },
        BufferSource{ "Indices", std::as_bytes(std::span{ data.indices }), rhi::BufferUsage::Index },
        BufferSource{ "Meshlets", std::as_bytes(std::span{ data.meshlets }) },
        BufferSource{ "MeshletVertices", std::as_bytes(std::span{ data.meshletVertices }) },
        BufferSource{ "MeshletPrimitives", std::as_bytes(std::span{ data.meshletPrimitives }) },
        BufferSource{ "MeshData", {} }, // Filled below, once the other addresses are known
    };

    std::array<rhi::BufferHandle, BufferCount> buffers{};
    std::size_t created{ 0 };
    const auto freeCreated = [&] {
        for (std::size_t i = 0; i < created; ++i)
            device.free(buffers[i]);
    };

    // The primitive stream is bytes; round every size up so copies stay 4-byte aligned.
    for (; created < BufferCount; ++created)
    {
        const auto& source = sources[created];
        const auto size = created == MeshDataBlock ? sizeof(GpuData) : base::alignTo(source.bytes.size(), 4);
        auto buffer = device.createBuffer({
            .name = std::format("{}{}", name, source.label),
            .size = size,
            .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::ShaderDeviceAddress |
                     rhi::BufferUsage::TransferDst | source.extraUsage,
            .memory = rhi::MemoryUsage::GpuOnly,
        });
        if (!buffer)
        {
            freeCreated();
            return makeError(buffer.error(), std::format("Failed to create {} buffer of mesh '{}'", source.label, name));
        }
        buffers[created] = *buffer;
    }

    const GpuData gpuData{
        .vertices = device.get(buffers[Vertices]).deviceAddress(),
        .indices = device.get(buffers[Indices]).deviceAddress(),
        .meshlets = device.get(buffers[Meshlets]).deviceAddress(),
        .meshletVertices = device.get(buffers[MeshletVertices]).deviceAddress(),
        .meshletPrimitives = device.get(buffers[MeshletPrimitives]).deviceAddress(),
        .vertexCount = static_cast<std::uint32_t>(data.vertices.size()),
        .indexCount = static_cast<std::uint32_t>(data.indices.size()),
        .meshletCount = static_cast<std::uint32_t>(data.meshlets.size()),
        .bounds = data.bounds,
    };
    sources[MeshDataBlock].bytes = std::as_bytes(std::span{ &gpuData, 1 });

    for (std::size_t i = 0; i < BufferCount; ++i)
    {
        // An unaligned tail (the byte-sized primitive stream) is uploaded padded with zeros.
        auto bytes = sources[i].bytes;
        std::vector<std::byte> padded;
        if (bytes.size() % 4 != 0)
        {
            padded.assign(bytes.begin(), bytes.end());
            padded.resize(base::alignTo(bytes.size(), 4));
            bytes = padded;
        }

        if (auto uploaded = detail::uploadBlocking(device, buffers[i], bytes); !uploaded)
        {
            freeCreated();
            return makeError(uploaded.error(), std::format("Failed to upload {} of mesh '{}'", sources[i].label, name));
        }
    }

    return std::shared_ptr<StaticMesh>{ new StaticMesh{ device, buffers, gpuData } };
}

StaticMesh::StaticMesh(rhi::Device& device, std::array<rhi::BufferHandle, BufferCount> buffers,
    const GpuData& gpuData) :
    m_device{ device },
    m_buffers{ buffers },
    m_gpuData{ gpuData }
{
}

StaticMesh::~StaticMesh()
{
    for (const auto buffer : m_buffers)
        m_device.free(buffer);
}

auto StaticMesh::meshDataAddress() const -> rhi::DeviceAddress
{
    return m_device.get(m_buffers[MeshDataBlock]).deviceAddress();
}
}
