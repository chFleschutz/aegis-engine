module indirect_draw;

import modules.common;
import modules.surface;

// The GPU-driven path's instance data (render_graph::InstanceData) and the push constants its
// geometry stages share (render_graph::GpuDrivenGeometryConstants). This module declares no push
// constant variable: each entry-point file declares its own, and a module's variable would make every
// importer's entry point use two (see shaders/modules/README.md).

namespace indirectDraw
{
    /// Instance::flags bits (render_graph::InstanceFlags).
    public static const uint INSTANCE_MIRRORED = 1 << 0; // Negative determinant: triangles wind the other way

    /// GeometryConstants::flags bits (render_graph::GeometryFlags).
    public static const uint GEOMETRY_CULL_BACK_FACING = 1 << 0; // The batch culls back faces, so its meshlets facing away go too

    public struct Instance
    {
        public float3x4 model; // Top three rows of the model matrix
        public float3 normalRow0;
        public float3 normalRow1;
        public float3 normalRow2;
        public uint batchSlot; // Its slot in its draw batch (render_graph::Instance::batchSlot)
        public common::Mesh* mesh;
        public MaterialAddress material;
        public uint drawBatch;
        public uint flags; // INSTANCE_* bits

        public property bool mirrored
        {
            get { return (flags & INSTANCE_MIRRORED) != 0; }
        }

        public property float4x4 modelMatrix
        {
            get { return float4x4(model, float4(0.0, 0.0, 0.0, 1.0)); }
        }

        public property float3x3 normalMatrix
        {
            get { return float3x3(normalRow0, normalRow1, normalRow2); }
        }
    }

    /// Where a draw batch's instances live in the visibility and indirect command buffers
    /// (render_graph::DrawBatchData): from 'offset' on, one entry per instance of the batch.
    public struct DrawBatch
    {
        public uint offset;
        public uint count;
    }

    /// VkDrawMeshTasksIndirectCommandEXT. Culling writes one per instance; it launches no task group for a culled one.
    public struct DrawMeshTasksIndirectCommand
    {
        public uint groupCountX;
        public uint groupCountY;
        public uint groupCountZ;
    }

    /// The push constants of the task, mesh and fragment stages; their files declare
    /// [[vk::push_constant]] ConstantBuffer<indirectDraw::GeometryConstants> push.
    public struct GeometryConstants
    {
        public common::Camera* camera;
        public Instance* staticInstances;
        public Instance* dynamicInstances;
        public uint* visibility; // Visible instance IDs, grouped by draw batch
        public uint batchFirst;  // Where this batch's IDs start in 'visibility'
        public uint batchSize;
        public uint staticCount;
        public uint dynamicCount;
        public uint flags; // GEOMETRY_* bits

        /// Whether the task stage culls the meshlets whose normal cone faces away from the camera. A batch that
        /// culls nothing (a double-sided material) draws their back faces.
        public property bool cullsBackFacing
        {
            get { return (flags & GEOMETRY_CULL_BACK_FACING) != 0; }
        }

        public func getInstance(uint id) -> Instance
        {
            return indirectDraw::getInstance(staticInstances, dynamicInstances, staticCount, id);
        }
    }

    /// Instance IDs count the static instances first, then the dynamic ones.
    public func getInstance(Instance* staticInstances, Instance* dynamicInstances, uint staticCount, uint id) -> Instance
    {
        if (id < staticCount)
            return staticInstances[id];
        return dynamicInstances[id - staticCount];
    }
}
