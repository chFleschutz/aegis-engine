import modules.common;
import modules.constants;
import modules.tbn;
import modules.bindless;

static const uint NUM_THREADS = 32;
static const uint MAX_VERTICES = 64;
static const uint MAX_PRIMITIVES = 126;

struct Global
{
    float4x4 projection;
    float4x4 view;
    float4x4 viewInverse;
}

struct TaskPayload
{
    // Empty for now
    uint dummy;
};

struct MSOut
{
    float4 position: SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
};

struct Material
{
    float3 albedo;
    float3 emissive;
    float metallic;
    float roughness;
    float ao;
    bindless::Handle<SampledImage2D> albedoMap;
    bindless::Handle<SampledImage2D> normalMap;
    bindless::Handle<SampledImage2D> metalRoughnessMap;
    bindless::Handle<SampledImage2D> aoMap;
    bindless::Handle<SampledImage2D> emissiveMap;
};

struct Push
{
    float3x4 modelRaw;
    float3 normalRow0; 
    bindless::Handle<UniformBuffer<Global>> global;
    float3 normalRow1;
    bindless::Handle<UniformBuffer<common::Mesh>> mesh;
    float3 normalRow2;
    bindless::Handle<UniformBuffer<Material>> material;

    property float4x4 modelMatrix
    {
        get { return float4x4(modelRaw, float4(0.0, 0.0, 0.0, 1.0)); }
    }

    property float3x3 normalMatrix
    {
        get { return float3x3(normalRow0, normalRow1, normalRow2); }
    }
};

[vk::push_constant] Push push;

// Mesh Shader --------------------

[shader("mesh")]
[numthreads(NUM_THREADS, 1, 1)]
[outputtopology("triangle")]
func meshMain(
    uint3 groupID: SV_GroupID,
    uint3 threadID: SV_GroupThreadID,
    in payload TaskPayload taskPayload,
    out vertices MSOut meshVertices[MAX_VERTICES],
    out indices uint3 meshPrimitives[MAX_PRIMITIVES])
{
    let global = push.global.get();
    let mesh = push.mesh.get();
    let meshlet = mesh.meshlet.get()[groupID.x];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    float4x4 viewProjection = mul(global.projection, global.view);

    // Emit vertices
    for (uint i = threadID.x; i < uint(meshlet.vertexCount); i += NUM_THREADS)
    {
        uint index = mesh.meshletIndex.get()[meshlet.vertexOffset + i];
        let v = mesh.vertex.get()[index];

        float4 worldPos = mul(push.modelMatrix, float4(v.position, 1.0));
        meshVertices[i].position = mul(viewProjection, worldPos);
        meshVertices[i].worldPosition = worldPos.xyz;
        meshVertices[i].worldNormal = normalize(mul(push.normalMatrix, v.normal));
        meshVertices[i].uv = v.uv;
    }

    // Emit primitives
    for (uint i = threadID.x; i < uint(meshlet.primitiveCount); i += NUM_THREADS)
    {
        uint offset = meshlet.primitiveOffset + i * 3;
        meshPrimitives[i] = uint3(
            mesh.meshletPrimitive.get()[offset + 0], 
            mesh.meshletPrimitive.get()[offset + 1],
            mesh.meshletPrimitive.get()[offset + 2]);
    }
}

// Fragment Shader --------------------

[shader("fragment")]
func fragmentMain(MSOut input, out common::GBuffer output)
{
    let mat = push.material.get();

    float3 albedo = mat.albedoMap.get().Sample(input.uv).rgb * mat.albedo;
    float3 normal = mat.normalMap.get().Sample(input.uv).rgb * 2.0 - 1.0;
    float3 emissive = mat.emissiveMap.get().Sample(input.uv).rgb * mat.emissive;
    float2 metalicRoughness = mat.metalRoughnessMap.get().Sample(input.uv).bg;
    float metallic = metalicRoughness.r * mat.metallic;
    float roughness = metalicRoughness.g * mat.roughness;
    float ao = mat.aoMap.get().Sample(input.uv).r * mat.ao;

    let TBN = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(normal) < 0.1 ? input.worldNormal : mul(normal, TBN));

    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(albedo, 1.0);
    output.arm = float4(ao, roughness, metallic, 0.0);
    output.emissive = float4(emissive, 1.0);
}
