import modules.bindless;
import modules.common;
import modules.constants;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.tbn;

static const uint MESH_GROUP_SIZE = 32;
static const uint MAX_VERTICES = 64;
static const uint MAX_PRIMITIVES = 126;

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
}

struct MSOut 
{
    float4 position : SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
    nointerpolation bindless::Handle<UniformBuffer<Material>> material;
}

// Mesh Shader --------------------

[shader("mesh")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
[outputtopology("triangle")]
func meshMain(
    uint3 groupID: SV_GroupID, 
    uint3 threadID: SV_GroupThreadID,
    in payload TaskPayload inPayload,
    out vertices MSOut meshVertices[MAX_VERTICES], 
    out indices uint3 meshPrimitives[MAX_PRIMITIVES])
{
    let camera = indirectDraw::pc.camera.get();
    let instance = indirectDraw::getInstance(inPayload.instanceID);
    let mesh = instance.mesh.get();
    let meshletID = inPayload.groupMeshletOffset + inPayload.meshletIDs[groupID.x];
    let meshlet = mesh.meshlet.get()[meshletID];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    // Emit vertices
    for (uint i = threadID.x; i < uint(meshlet.vertexCount); i += MESH_GROUP_SIZE)
    {
        uint index = mesh.meshletIndex.get()[meshlet.vertexOffset + i];
        let v = mesh.vertex.get()[index];

        float4 worldPos = mul(instance.modelMatrix, float4(v.position, 1.0));
        meshVertices[i].position = mul(camera.viewProjection, worldPos);
        meshVertices[i].worldPosition = worldPos.xyz;
        meshVertices[i].worldNormal = normalize(mul(instance.normalMatrix, v.normal));
        meshVertices[i].uv = v.uv;
        meshVertices[i].material = instance.material.asHandle<UniformBuffer<Material>>();
    }

    // Emit primitives
    for (uint i = threadID.x; i < uint(meshlet.primitiveCount); i += MESH_GROUP_SIZE)
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
    let mat = input.material.get();

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
