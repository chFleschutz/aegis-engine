module meshlet_cull;

public static const uint TASK_GROUP_SIZE = 32;

// The most a meshlet holds: assets::meshletMaxVertices and assets::meshletMaxPrimitives, which
// MeshPreprocessor enforces (a unit test compares them with these).
public static const uint MESHLET_MAX_VERTICES = 64;
public static const uint MESHLET_MAX_PRIMITIVES = 126;

public struct TaskPayload
{
    public uint8_t meshletIDs[TASK_GROUP_SIZE];
    public uint instanceID;
    public uint groupMeshletOffset;
}
