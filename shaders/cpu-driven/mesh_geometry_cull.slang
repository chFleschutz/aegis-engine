import modules.common;
import modules.constants;
import modules.tbn;
import modules.bindless;
import modules.visibility;

static const uint TASK_GROUP_SIZE = 32;
static const uint MESH_GROUP_SIZE = 32;
static const uint MAX_VERTICES = 64;
static const uint MAX_PRIMITIVES = 126;

struct MSOut
{
    float4 position : SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
};

struct Global
{
    float4x4 projection;
    float4x4 view;
    float4x4 viewInverse;
    common::Frustum frustum;
    float3 cameraPosition;
}

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

struct PushConstant
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
[vk::push_constant] PushConstant pc;

struct TaskPayload
{
    uint8_t meshletIDs[TASK_GROUP_SIZE];
    uint groupMeshletOffset;
}
groupshared TaskPayload sharedPayload;

// Task Shader --------------------

[shader("task")]
[numthreads(TASK_GROUP_SIZE, 1, 1)]
func taskMain(
    uint3 groupID: SV_GroupID,
    uint3 groupThreadID: SV_GroupThreadID,
    uint3 dispatchThreadID: SV_DispatchThreadID)
{
    let mesh = pc.mesh.get();

    bool meshletVisible = false;
    if (dispatchThreadID.x < mesh.meshletCount)
    {
        let global = pc.global.get();
        let meshlet = mesh.meshlets.get()[dispatchThreadID.x];
        let modelMatrix = pc.modelMatrix;
        let worldBounds = meshlet.bounds.transform(modelMatrix);
        let worldConeAxis = normalize(mul((float3x3)modelMatrix, meshlet.cone.axis));

        meshletVisible = visibility::frustumVisible(worldBounds, global.frustum)
            && visibility::coneVisible(worldBounds, worldConeAxis, meshlet.cone, global.cameraPosition);
    }

    uint numGroupVisible = WaveActiveCountBits(meshletVisible);
    uint groupIdxOffset = WavePrefixCountBits(meshletVisible);

    if (meshletVisible)
    {
        sharedPayload.meshletIDs[groupIdxOffset] = uint8_t(groupThreadID.x);
    }

    if (groupThreadID.x == 0)
    {
        sharedPayload.groupMeshletOffset = groupID.x * TASK_GROUP_SIZE;
        DispatchMesh(numGroupVisible, 1, 1, sharedPayload);
    }
}


// Mesh Shader --------------------

[shader("mesh")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
[outputtopology("triangle")]
func meshMain(
    uint3 groupID: SV_GroupID,
    uint3 groupThreadID: SV_GroupThreadID,
    out vertices MSOut meshVertices[MAX_VERTICES],
    out indices uint3 meshPrimitives[MAX_PRIMITIVES])
{
    let global = pc.global.get();
    let mesh = pc.mesh.get();

    let meshletID = sharedPayload.groupMeshletOffset + sharedPayload.meshletIDs[groupID.x];
    let meshlet = mesh.meshlets.get()[meshletID];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    float4x4 viewProjection = mul(global.projection, global.view);

    // Emit vertices
    for (uint i = groupThreadID.x; i < uint(meshlet.vertexCount); i += MESH_GROUP_SIZE)
    {
        uint vertexIndex = mesh.meshletVertices.get()[meshlet.vertexOffset + i];
        let vertex = mesh.vertices.get()[vertexIndex];

        float4 worldPos = mul(pc.modelMatrix, float4(vertex.position, 1.0));
        meshVertices[i].position = mul(viewProjection, worldPos);
        meshVertices[i].worldPosition = worldPos.xyz;
        meshVertices[i].worldNormal = normalize(mul(pc.normalMatrix, vertex.normal));
        meshVertices[i].uv = vertex.uv;
    }

    // Emit primitives
    for (uint i = groupThreadID.x; i < uint(meshlet.primitiveCount); i += MESH_GROUP_SIZE)
    {
        uint offset = meshlet.primitiveOffset + (i * 3);
        meshPrimitives[i] = uint3(
            mesh.meshletPrimitives.get()[offset + 0], 
            mesh.meshletPrimitives.get()[offset + 1],
            mesh.meshletPrimitives.get()[offset + 2]);
    }
}

// Fragment Shader --------------------

[shader("fragment")]
func fragmentMain(MSOut input, out common::GBuffer output)
{
    let mat = pc.material.get();

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
