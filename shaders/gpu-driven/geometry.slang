import modules.common;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.surface;
import SURFACE_MODULE;

// GPU-driven G-buffer technique, mesh and fragment stages: the mesh shader emits the meshlets task.slang
// kept (one task group per 32 meshlets of a visible instance). Built once per surface shader, which
// SURFACE_MODULE names (see modules/surface.slang).

static const uint MESH_GROUP_SIZE = 32;

[[vk::push_constant]] ConstantBuffer<indirectDraw::GeometryConstants> push;

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
    out vertices MSOut meshVertices[MESHLET_MAX_VERTICES],
    out indices uint3 meshPrimitives[MESHLET_MAX_PRIMITIVES])
{
    let instance = push.getInstance(inPayload.instanceID);
    let mesh = instance.mesh;
    let meshlet = mesh->meshlets[inPayload.groupMeshletOffset + inPayload.meshletIDs[groupID.x]];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);

    for (uint i = threadID.x; i < uint(meshlet.vertexCount); i += MESH_GROUP_SIZE)
    {
        let vertex = mesh->vertices[mesh->meshletVertices[meshlet.vertexOffset + i]];
        float4 worldPos = mul(instance.modelMatrix, float4(vertex.position, 1.0));
        meshVertices[i].position = mul(push.camera->viewProjection, worldPos);
        meshVertices[i].worldPosition = worldPos.xyz;
        meshVertices[i].worldNormal = normalize(mul(instance.normalMatrix, vertex.normal));
        meshVertices[i].uv = vertex.uv;
        meshVertices[i].instanceID = inPayload.instanceID;
    }

    // A mirrored transform reverses the winding of every triangle; swapping two indices restores it, so
    // the pipeline's back-face culling keeps the outside.
    let mirrored = instance.mirrored;
    for (uint i = threadID.x; i < uint(meshlet.primitiveCount); i += MESH_GROUP_SIZE)
    {
        let offset = meshlet.primitiveOffset + i * 3;
        let first = mesh->meshletPrimitives[offset + 0];
        let second = mesh->meshletPrimitives[offset + 1];
        let third = mesh->meshletPrimitives[offset + 2];
        meshPrimitives[i] = mirrored ? uint3(first, third, second) : uint3(first, second, third);
    }
}

[shader("fragment")]
func fragmentMain(MSOut input, out common::GBuffer output)
{
    let material = (Surface.Params*)push.getInstance(input.instanceID).material;

    SurfaceInput surfaceInput;
    surfaceInput.uv = input.uv;
    surfaceInput.worldPosition = input.worldPosition;
    surfaceInput.worldNormal = input.worldNormal;

    let surface = Surface.evaluate(material, surfaceInput);
    output = toGBuffer(surfaceInput, surface, Surface.shadingModel());
}
