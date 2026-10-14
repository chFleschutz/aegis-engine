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

func getInstance(uint index) -> indirectDraw::Instance
{
    if (index < pc.staticCount)
        return pc.staticInstances[index];
    return pc.dynamicInstances[index - pc.staticCount];
}

[shader("compute")]
[numthreads(64, 1, 1)]
func main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    let instanceID = dispatchThreadID.x;
    if (instanceID >= pc.staticCount + pc.dynamicCount)
        return;

    let instance = getInstance(instanceID);
    let mesh = instance.mesh;

    let worldBounds = mesh->bounds.transform(instance.modelMatrix);
    if (!visibility::frustumVisible(worldBounds, pc.camera->frustum))
        return;

    uint drawID;
    InterlockedAdd(pc.counts[instance.drawBatch], 1, drawID);

    let batch = pc.drawBatches[instance.drawBatch];
    pc.visibility[batch.offset + drawID] = instanceID;

    let groupCountX = (mesh->meshletCount + TASK_GROUP_SIZE - 1) / TASK_GROUP_SIZE;
    pc.commands[batch.offset + drawID] = DrawMeshTasksIndirectCommand(groupCountX, 1, 1);
}
