export module aegis.assets:gpu_types;
import std;
import aegis.rhi;
import aegis.math;

// The C++ side of the data the shaders read from asset buffers, each struct named after (and mirroring,
// member for member, in scalar layout) its twin in shaders/modules/common.slang. tests/assets/gpu_types.test.cpp
// pins sizes and offsets. Change a struct here and its Slang twin together. The render graph's own mirrors
// (push constants, instances) are in aegis.render_graph:gpu_types.

export namespace aegis::assets
{
/// @brief The most vertices and primitives (triangles) a meshlet holds: MESHLET_MAX_VERTICES and
///        MESHLET_MAX_PRIMITIVES in shaders/modules/meshlet_cull.slang, which sizes the mesh shader's
///        output arrays. MeshPreprocessor rejects larger limits.
inline constexpr std::uint32_t meshletMaxVertices{ 64 };
inline constexpr std::uint32_t meshletMaxPrimitives{ 126 };

/// @brief common::Vertex: interleaved vertex. Shaders read it through a buffer device address (no vertex
///        input).
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 color;
};

/// @brief common::BoundingSphere
struct BoundingSphere
{
    glm::vec3 center{ 0.0f };
    float radius{ 0.0f };
};

/// @brief common::Meshlet: a cluster of up to meshletMaxVertices vertices / meshletMaxPrimitives
///        triangles, as meshoptimizer builds them.
struct Meshlet
{
    BoundingSphere bounds;
    std::int8_t coneAxis[3]; // common::NormalCone: normalized to [-127, 127]
    std::int8_t coneCutoff;
    std::uint32_t vertexOffset;    // Into MeshData::meshletVertices
    std::uint32_t primitiveOffset; // Into MeshData::meshletPrimitives, in bytes (3 per triangle)
    std::uint8_t vertexCount;
    std::uint8_t primitiveCount;
    // The stride's tail, explicit and zeroed: implicit padding is copied uninitialized, so writing equal meshes
    // to the import cache gave different bytes.
    std::uint8_t padding[2]{};
};

/// @brief common::Mesh: a whole mesh, reached through one address (StaticMesh::meshDataAddress).
struct MeshGpuData
{
    rhi::DeviceAddress vertices;          // Vertex*
    rhi::DeviceAddress indices;           // uint32_t*
    rhi::DeviceAddress meshlets;          // Meshlet*
    rhi::DeviceAddress meshletVertices;   // uint32_t*
    rhi::DeviceAddress meshletPrimitives; // uint8_t*
    std::uint32_t vertexCount;
    std::uint32_t indexCount;
    std::uint32_t meshletCount;
    BoundingSphere bounds;
};

/// @brief common::PbrMaterial: the parameter block of the default PBR material. The template
///        DefaultAssets::pbrTemplate (detail::addPbrParameters) must lay its parameters out like this.
struct PbrMaterialBlock
{
    glm::vec3 albedo;
    glm::vec3 emissive;
    float metallic;
    float roughness;
    float ambientOcclusion;
    rhi::TextureDescriptor albedoMap;
    rhi::TextureDescriptor normalMap;
    rhi::TextureDescriptor metalRoughnessMap;
    rhi::TextureDescriptor ambientOcclusionMap;
    rhi::TextureDescriptor emissiveMap;
};
}
