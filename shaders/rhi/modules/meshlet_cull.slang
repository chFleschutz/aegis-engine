module meshlet_cull;

public static const uint TASK_GROUP_SIZE = 32;

public struct TaskPayload
{
    public uint8_t meshletIDs[TASK_GROUP_SIZE];
    public uint instanceID;
    public uint groupMeshletOffset;
}
