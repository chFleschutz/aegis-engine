import modules.common;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.visibility;

// GPU-driven G-buffer pass, task stage: one indirect mesh-task draw per visible instance of a draw
// batch. Culls the instance's meshlets (frustum and normal cone) and launches mesh groups for the rest.

groupshared TaskPayload sharedPayload;

[shader("task")]
[numthreads(TASK_GROUP_SIZE, 1, 1)]
func taskMain(
    uint drawID : SV_DrawIndex,
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 dispatchThreadID : SV_DispatchThreadID)
{
    let pc = indirectDraw::pc;
    if (drawID >= pc.batchSize)
        return;

    let instanceID = pc.visibility[pc.batchFirst + drawID];
    let instance = indirectDraw::getInstance(instanceID);
    let mesh = instance.mesh;

    bool meshletVisible = false;
    if (dispatchThreadID.x < mesh->meshletCount)
    {
        let camera = pc.camera;
        let meshlet = mesh->meshlets[dispatchThreadID.x];
        let worldBounds = meshlet.bounds.transform(instance.modelMatrix);
        let worldConeAxis = normalize(mul((float3x3)instance.modelMatrix, meshlet.cone.axis));

        meshletVisible = visibility::frustumVisible(worldBounds, camera->frustum)
            && visibility::coneVisible(worldBounds, worldConeAxis, meshlet.cone, camera->position);
    }

    uint visibleCount = WaveActiveCountBits(meshletVisible);
    uint visibleIndex = WavePrefixCountBits(meshletVisible);
    if (meshletVisible)
        sharedPayload.meshletIDs[visibleIndex] = uint8_t(groupThreadID.x);

    if (groupThreadID.x == 0)
    {
        sharedPayload.groupMeshletOffset = groupID.x * TASK_GROUP_SIZE;
        sharedPayload.instanceID = instanceID;
        DispatchMesh(visibleCount, 1, 1, sharedPayload);
    }
}
