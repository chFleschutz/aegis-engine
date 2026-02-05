import modules.common;
import modules.constants;
import modules.tbn;

struct Global
{
    float4x4 projection;
    float4x4 view;
    float4x4 viewInverse;
}

struct Push
{
    float4x4 model;
    float4x4 normal;
};

struct Material
{
    float3 albedo;
    float3 emissive;
    float metallic;
    float roughness;
    float ao;
};

struct MSOut
{
    float4 position : SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
};

static const uint NUM_THREADS = 32;
static const uint MAX_VERTICES = 64;
static const uint MAX_PRIMITIVES = 126;

[vk::push_constant] ConstantBuffer<Push> push;
[vk::binding(0, 0)] ConstantBuffer<Global> global;

[vk::binding(0, 1)] ConstantBuffer<Material> material;
[vk::binding(1, 1)] Sampler2D albedoMap;
[vk::binding(2, 1)] Sampler2D normalMap;
[vk::binding(3, 1)] Sampler2D metalRoughnessMap;
[vk::binding(4, 1)] Sampler2D aoMap;
[vk::binding(5, 1)] Sampler2D emissiveMap;

[vk::binding(0, 2)] StructuredBuffer<common::Meshlet> meshlets;
[vk::binding(0, 3)] StructuredBuffer<uint> vertexIndices;
[vk::binding(1, 3)] StructuredBuffer<uint8_t, ScalarDataLayout> primitives;
[vk::binding(2, 3)] StructuredBuffer<common::Vertex, ScalarDataLayout> vertices;

[shader("mesh")]
[numthreads(NUM_THREADS, 1, 1)]
[outputtopology("triangle")]
func meshMain(
    uint3 groupID: SV_GroupID,
    uint3 threadID: SV_GroupThreadID,
    out vertices MSOut meshVertices[MAX_VERTICES],
    out indices uint3 meshPrimitives[MAX_PRIMITIVES])
{
    var meshlet = meshlets[groupID.x];
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    float4x4 viewProjection = mul(global.projection, global.view);

    // Emit vertices
    for (uint i = threadID.x; i < uint(meshlet.vertexCount); i += NUM_THREADS)
    {
        uint index = vertexIndices[meshlet.vertexOffset + i];
        let v = vertices[index];

        float4 worldPos = mul(push.model, float4(v.position, 1.0));
        meshVertices[i].position = mul(viewProjection, worldPos);
        meshVertices[i].worldPosition = worldPos.xyz;
        meshVertices[i].worldNormal = normalize(mul(float3x3(push.normal), v.normal));
        meshVertices[i].uv = v.uv;
    }

    // Emit primitives
    for (uint i = threadID.x; i < uint(meshlet.primitiveCount); i += NUM_THREADS)
    {
        uint offset = meshlet.primitiveOffset + i * 3;
        meshPrimitives[i] = uint3(primitives[offset + 0], primitives[offset + 1], primitives[offset + 2]);
    }
}

[shader("fragment")]
func fragmentMain(MSOut input, out common::GBuffer output) 
{
    float3 albedo = albedoMap.Sample(input.uv).rgb * material.albedo;
    float3 normal = normalMap.Sample(input.uv).rgb * 2.0 - 1.0;
    float3 emissive = emissiveMap.Sample(input.uv).rgb * material.emissive;
    float2 metalicRoughness = metalRoughnessMap.Sample(input.uv).bg;
    float metallic = metalicRoughness.r * material.metallic;
    float roughness = metalicRoughness.g * material.roughness;
    float ao = aoMap.Sample(input.uv).r * material.ao;

    let TBN = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(normal) < 0.1 ? input.worldNormal : mul(normal, TBN));

    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(albedo, 1.0);
    output.arm = float4(ao, roughness, metallic, 0.0);
    output.emissive = float4(emissive, 1.0);
}
