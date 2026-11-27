import modules.indirect_draw;

// Draw compaction: packs each draw batch's visible instances to the front of its range of the visibility and
// indirect command buffers, in slot order, and writes the batch's draw count. One workgroup per batch. Culling wrote
// every instance at its slot, with a command that launches no task group if it was culled.
//
// Packing in place is safe: an entry moves only towards its batch's start, never past an entry the group has yet to
// read, and the group reads a whole chunk before it writes any of it.

static const uint GROUP_SIZE = 64; // render_graph::compactionGroupSize
static const uint MASK_WORDS = GROUP_SIZE / 32;

struct PushConstants
{
    indirectDraw::DrawBatch* drawBatches;
    uint* visibility;
    indirectDraw::DrawMeshTasksIndirectCommand* commands;
    uint* counts; // One per draw batch
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

groupshared uint visibleMasks[MASK_WORDS]; // Bit i of the chunk: its entry i is visible
groupshared uint packedCount;              // Visible entries of the chunks before this one

[shader("compute")]
[numthreads(GROUP_SIZE, 1, 1)]
func main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    let batchID = groupID.x;
    let batch = push.drawBatches[batchID];
    let lane = groupThreadID.x;
    if (lane == 0)
        packedCount = 0;

    // The bound is the same for the whole group, so every invocation reaches every barrier.
    for (uint start = 0; start < batch.count; start += GROUP_SIZE)
    {
        if (lane < MASK_WORDS)
            visibleMasks[lane] = 0;
        GroupMemoryBarrierWithGroupSync();

        let slot = start + lane;
        var instanceID = 0u;
        var command = indirectDraw::DrawMeshTasksIndirectCommand(0, 1, 1);
        if (slot < batch.count)
        {
            instanceID = push.visibility[batch.offset + slot];
            command = push.commands[batch.offset + slot];
        }
        let visible = command.groupCountX != 0;
        if (visible)
            InterlockedOr(visibleMasks[lane / 32], 1u << (lane % 32));
        // Device memory too: the writes below may land on entries other invocations read above.
        AllMemoryBarrierWithGroupSync();

        // The entry's rank among the chunk's visible ones: the visible bits below its own.
        var rank = countbits(visibleMasks[lane / 32] & ((1u << (lane % 32)) - 1u));
        var chunkCount = 0u;
        for (uint word = 0; word < MASK_WORDS; ++word)
        {
            let count = countbits(visibleMasks[word]);
            chunkCount += count;
            if (word < lane / 32)
                rank += count;
        }
        if (visible)
        {
            let packed = batch.offset + packedCount + rank;
            push.visibility[packed] = instanceID;
            push.commands[packed] = command;
        }
        GroupMemoryBarrierWithGroupSync();
        if (lane == 0)
            packedCount += chunkCount;
    }

    // Only lane 0 writes packedCount, so it reads its own last write here.
    if (lane == 0)
        push.counts[batchID] = packedCount;
}
