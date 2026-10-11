import rhi.modules.bindless;
import rhi.modules.common;
import rhi.modules.indirect_draw;
import rhi.modules.meshlet_cull;
import rhi.modules.tbn;

// GPU-driven G-buffer pass, mesh and fragment stages: the mesh shader emits the meshlets task.slang
// kept (one task group per 32 meshlets of a visible instance).

static const uint MESH_GROUP_SIZE = 32;
static const uint MAX_VERTICES = 64;
static const uint MAX_PRIMITIVES = 126;

struct MSOut
{
    float4 position : SV_Position;
    [vk::location(0)] float3 worldPosition;
    [vk::location(1)] float3 worldNormal;
    [vk::location(2)] float2 uv;
    [vk::location(3)] nointerpolation uint instanceID; // The fragment shader finds the material through it
}

[shader("mesh")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
[outputtopology("triangle")]
func meshMain(
    uint3 groupID : SV_GroupID,
    uint3 threadID : SV_GroupThreadID,
    in payload TaskPayload inPayload,
    out vertices MSOut meshVertices[MAX_VERTICES],
    out indices uint3 meshPrimitives[MAX_PRIMITIVES])
{
    let pc = indirectDraw::pc;
    let instance = indirectDraw::getInstance(inPayload.instanceID);
    let mesh = instance.mesh;
    let meshlet = mesh->meshlets[inPayload.groupMeshletOffset + inPayload.meshletIDs[groupID.x]];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    for (uint i = threadID.x; i < uint(meshlet.vertexCount); i += MESH_GROUP_SIZE)
    {
        let vertex = mesh->vertices[mesh->meshletVertices[meshlet.vertexOffset + i]];
        float4 worldPos = mul(instance.modelMatrix, float4(vertex.position, 1.0));
        meshVertices[i].position = mul(pc.camera->viewProjection, worldPos);
        meshVertices[i].worldPosition = worldPos.xyz;
        meshVertices[i].worldNormal = normalize(mul(instance.normalMatrix, vertex.normal));
        meshVertices[i].uv = vertex.uv;
        meshVertices[i].instanceID = inPayload.instanceID;
    }

    for (uint i = threadID.x; i < uint(meshlet.primitiveCount); i += MESH_GROUP_SIZE)
    {
        let offset = meshlet.primitiveOffset + i * 3;
        meshPrimitives[i] = uint3(
            mesh->meshletPrimitives[offset + 0],
            mesh->meshletPrimitives[offset + 1],
            mesh->meshletPrimitives[offset + 2]);
    }
}

[shader("fragment")]
func fragmentMain(MSOut input, out common::GBuffer output)
{
    let material = indirectDraw::getInstance(input.instanceID).material;

    float3 albedo = material->albedoMap.sample(input.uv).rgb * material->albedo;
    float3 normal = material->normalMap.sample(input.uv).rgb * 2.0 - 1.0;
    float3 emissive = material->emissiveMap.sample(input.uv).rgb * material->emissive;
    float2 metallicRoughness = material->metalRoughnessMap.sample(input.uv).bg;
    float metallic = metallicRoughness.r * material->metallic;
    float roughness = metallicRoughness.g * material->roughness;
    float ao = material->ambientOcclusionMap.sample(input.uv).r * material->ambientOcclusion;

    let tbn = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(normal) < 0.1 ? input.worldNormal : mul(normal, tbn));

    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(albedo, 1.0);
    output.arm = float4(ao, roughness, metallic, 0.0);
    output.emissive = float4(emissive, 1.0);
}
