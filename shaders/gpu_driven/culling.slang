import modules.common;
import modules.indirect_draw;
import modules.meshlet_cull;
import modules.visibility;

// Frustum-culls every instance and writes its ID and its indirect mesh-task command at its slot in its draw batch
// (Instance::batchSlot); a culled instance's command launches no task group. The draw compaction pass
// (compaction.slang) then packs each batch's visible instances in slot order, so the draws, and which of two surfaces
// at the same depth ends up in the frame, do not depend on the order the invocations run in.

struct PushConstants
{
    common::Camera* camera;
    indirectDraw::Instance* staticInstances;
    indirectDraw::Instance* dynamicInstances;
    indirectDraw::DrawBatch* drawBatches;
    uint* visibility;
    indirectDraw::DrawMeshTasksIndirectCommand* commands;
    uint staticCount;
    uint dynamicCount;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

[shader("compute")]
[numthreads(64, 1, 1)]
func main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    let instanceID = dispatchThreadID.x;
    if (instanceID >= push.staticCount + push.dynamicCount)
        return;

    let instance = indirectDraw::getInstance(push.staticInstances, push.dynamicInstances, push.staticCount, instanceID);
    let mesh = instance.mesh;

    // The store keeps a batch's slots below its instance count. Should the instance buffers hold more for this batch,
    // the extra ones would write into the next batch's slots.
    let batch = push.drawBatches[instance.drawBatch];
    if (instance.batchSlot >= batch.count)
        return;

    let worldBounds = mesh->bounds.transform(instance.modelMatrix);
    let visible = visibility::frustumVisible(worldBounds, push.camera->frustum);
    let groupCountX = visible ? (mesh->meshletCount + TASK_GROUP_SIZE - 1) / TASK_GROUP_SIZE : 0;

    let slot = batch.offset + instance.batchSlot;
    push.visibility[slot] = instanceID;
    push.commands[slot] = indirectDraw::DrawMeshTasksIndirectCommand(groupCountX, 1, 1);
}
