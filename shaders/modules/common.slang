module common;

import modules.bindless;

// Data the C++ side writes, mirrored member for member (scalar layout; see shaders/modules/README.md).

namespace common
{
    public struct Frustum
    {
        public float4 planes[6]; // render_graph::Frustum
    }

    /// render_graph::CameraData, written by the scene update pass once per frame.
    public struct Camera
    {
        public float4x4 view;
        public float4x4 projection;
        public float4x4 viewProjection;
        public Frustum frustum;
        public float3 position;
    }

    /// assets::Vertex
    public struct Vertex
    {
        public float3 position;
        public float3 normal;
        public float2 uv;
        public float3 color;
    }

    /// assets::BoundingSphere
    public struct BoundingSphere
    {
        public float3 center;
        public float radius;

        /// The radius grows by the largest axis scale: the length of the longest basis vector, a
        /// column of the matrix (mat[i] is row i).
        public func transform(float4x4 mat) -> BoundingSphere
        {
            let scaleX = length(float3(mat[0][0], mat[1][0], mat[2][0]));
            let scaleY = length(float3(mat[0][1], mat[1][1], mat[2][1]));
            let scaleZ = length(float3(mat[0][2], mat[1][2], mat[2][2]));
            let maxScale = max(scaleX, max(scaleY, scaleZ));
            return BoundingSphere(mul(mat, float4(center, 1.0)).xyz, radius * maxScale);
        }
    }

    public struct NormalCone
    {
        public int8_t axis_s8[3];
        public int8_t cutoff_s8;

        public property float3 axis
        {
            get { return float3(axis_s8[0], axis_s8[1], axis_s8[2]) / 127.0; }
        }

        public property float cutoff
        {
            get { return float(cutoff_s8) / 127.0; }
        }
    }

    /// assets::Meshlet
    public struct Meshlet
    {
        public BoundingSphere bounds;
        public NormalCone cone;
        public uint vertexOffset;
        public uint primitiveOffset;
        public uint8_t vertexCount;
        public uint8_t primitiveCount;
    }

    /// assets::StaticMesh::GpuData: a whole mesh, reached through one address.
    /// Struct pointers are stored as plain addresses and exposed as properties: slangc 2025.17
    /// (Vulkan SDK 1.4.328) emits a duplicate OpTypeForwardPointer for a struct pointer member of a
    /// struct that is itself reached through a pointer, which spirv-val rejects.
    public struct Mesh
    {
        uint64_t vertexAddress; // Vertex*
        public uint* indices;
        uint64_t meshletAddress; // Meshlet*
        public uint* meshletVertices;
        public uint8_t* meshletPrimitives;
        public uint vertexCount;
        public uint indexCount;
        public uint meshletCount;
        public BoundingSphere bounds;

        public property Vertex* vertices
        {
            get { return (Vertex*)vertexAddress; }
        }

        public property Meshlet* meshlets
        {
            get { return (Meshlet*)meshletAddress; }
        }
    }

    /// The default PBR material's parameter block (assets::DefaultAssets::pbrTemplate, in order).
    public struct PbrMaterial
    {
        public float3 albedo;
        public float3 emissive;
        public float metallic;
        public float roughness;
        public float ambientOcclusion;
        public bindless::TextureDescriptor albedoMap;
        public bindless::TextureDescriptor normalMap;
        public bindless::TextureDescriptor metalRoughnessMap;
        public bindless::TextureDescriptor ambientOcclusionMap;
        public bindless::TextureDescriptor emissiveMap;
    }

    public struct GBuffer
    {
        [vk::location(0)] public float4 position;
        [vk::location(1)] public float4 normal;
        [vk::location(2)] public float4 albedo;
        [vk::location(3)] public float4 arm;
        [vk::location(4)] public float4 emissive;
    }
}
