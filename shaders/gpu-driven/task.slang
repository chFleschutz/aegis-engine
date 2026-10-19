import modules.common;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.visibility;

// GPU-driven G-buffer pass, task stage: one indirect mesh-task draw per visible instance of a draw
// batch. Culls the instance's meshlets (frustum and normal cone) and launches mesh groups for the rest.

[[vk::push_constant]] ConstantBuffer<indirectDraw::GeometryConstants> pc;

groupshared TaskPayload sharedPayload;
groupshared uint visibleCount;

[shader("task")]
[numthreads(TASK_GROUP_SIZE, 1, 1)]
func taskMain(
    uint drawID : SV_DrawIndex,
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID,
    uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (groupThreadID.x == 0)
        visibleCount = 0;
    GroupMemoryBarrierWithGroupSync();

    // drawID is the same for the whole group, so the group either culls or emits no mesh groups.
    // No early return: every invocation must reach DispatchMesh exactly once.
    if (drawID < pc.batchSize)
    {
        let instanceID = pc.visibility[pc.batchFirst + drawID];
        let instance = pc.getInstance(instanceID);
        let mesh = instance.mesh;

        if (dispatchThreadID.x < mesh->meshletCount)
        {
            let camera = pc.camera;
            let meshlet = mesh->meshlets[dispatchThreadID.x];
            let worldBounds = meshlet.bounds.transform(instance.modelMatrix);
            let worldConeAxis = normalize(mul((float3x3)instance.modelMatrix, meshlet.cone.axis));

            let meshletVisible = visibility::frustumVisible(worldBounds, camera->frustum)
                && visibility::coneVisible(worldBounds, worldConeAxis, meshlet.cone, camera->position);

            // Compact through a workgroup counter, not wave ops: the group may span several
            // subgroups (task-stage subgroups can be narrower than TASK_GROUP_SIZE).
            if (meshletVisible)
            {
                uint visibleIndex;
                InterlockedAdd(visibleCount, 1, visibleIndex);
                sharedPayload.meshletIDs[visibleIndex] = uint8_t(groupThreadID.x);
            }
        }

        if (groupThreadID.x == 0)
        {
            sharedPayload.groupMeshletOffset = groupID.x * TASK_GROUP_SIZE;
            sharedPayload.instanceID = instanceID;
        }
    }

    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(visibleCount, 1, 1, sharedPayload);
}
