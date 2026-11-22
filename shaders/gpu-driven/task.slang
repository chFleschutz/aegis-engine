import modules.common;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.visibility;

// GPU-driven geometry passes (G-buffer and depth), task stage: one indirect mesh-task draw per visible instance of a draw
// batch. Culls the instance's meshlets (frustum, and normal cone if the batch culls back faces) and launches mesh
// groups for the rest.

[[vk::push_constant]] ConstantBuffer<indirectDraw::GeometryConstants> push;

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
    if (drawID < push.batchSize)
    {
        let instanceID = push.visibility[push.batchFirst + drawID];
        let instance = push.getInstance(instanceID);
        let mesh = instance.mesh;

        if (dispatchThreadID.x < mesh->meshletCount)
        {
            let camera = push.camera;
            let meshlet = mesh->meshlets[dispatchThreadID.x];
            let worldBounds = meshlet.bounds.transform(instance.modelMatrix);
            let worldConeAxis = normalize(mul((float3x3)instance.modelMatrix, meshlet.cone.axis));

            // A batch that culls no back faces (a double-sided material) draws the meshlets facing away too. An
            // orthographic camera (a shadow view's) sees along one direction, not from a point.
            var facing = true;
            if (push.cullsBackFacing)
            {
                facing = camera->isOrthographic
                    ? visibility::coneVisibleAlong(worldConeAxis, meshlet.cone, camera->forward)
                    : visibility::coneVisible(worldBounds, worldConeAxis, meshlet.cone, camera->position);
            }
            let meshletVisible = visibility::frustumVisible(worldBounds, camera->frustum) && facing;

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
