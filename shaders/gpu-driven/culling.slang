import modules.common;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.visibility;

// Frustum-culls every instance and, for each visible one, appends its ID to its draw batch and
// writes the batch's indirect mesh-task command.

struct DrawBatch
{
    uint offset;
    uint count;
}

struct DrawMeshTasksIndirectCommand
{
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
}

struct PushConstants
{
    common::Camera* camera;
    indirectDraw::Instance* staticInstances;
    indirectDraw::Instance* dynamicInstances;
    DrawBatch* drawBatches;
    uint* visibility;
    DrawMeshTasksIndirectCommand* commands;
    uint* counts; // One per draw batch, cleared to 0 before this pass
    uint staticCount;
    uint dynamicCount;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[shader("compute")]
[numthreads(64, 1, 1)]
func main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    let instanceID = dispatchThreadID.x;
    if (instanceID >= pc.staticCount + pc.dynamicCount)
        return;

    let instance = indirectDraw::getInstance(pc.staticInstances, pc.dynamicInstances, pc.staticCount, instanceID);
    let mesh = instance.mesh;

    let worldBounds = mesh->bounds.transform(instance.modelMatrix);
    if (!visibility::frustumVisible(worldBounds, pc.camera->frustum))
        return;

    uint drawID;
    InterlockedAdd(pc.counts[instance.drawBatch], 1, drawID);

    // The batch holds slots for the instances the registry counts. Should the instance buffers hold
    // more for this batch, the extra ones would write into the next batch's slots.
    let batch = pc.drawBatches[instance.drawBatch];
    if (drawID >= batch.count)
        return;

    pc.visibility[batch.offset + drawID] = instanceID;

    let groupCountX = (mesh->meshletCount + TASK_GROUP_SIZE - 1) / TASK_GROUP_SIZE;
    pc.commands[batch.offset + drawID] = DrawMeshTasksIndirectCommand(groupCountX, 1, 1);
}
