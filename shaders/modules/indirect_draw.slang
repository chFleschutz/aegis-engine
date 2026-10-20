module indirect_draw;

import modules.common;

// The GPU-driven path's instance data (render_graph::InstanceData) and the push constants its
// geometry stages share (render_graph::GpuDrivenGeometryConstants). This module declares no push
// constant variable: each entry-point file declares its own, and a module's variable would make every
// importer's entry point use two (see shaders/modules/README.md).

namespace indirectDraw
{
    public struct Instance
    {
        public float3x4 model; // Top three rows of the model matrix
        public float3 normalRow0;
        public float3 normalRow1;
        public float3 normalRow2;
        public common::Mesh* mesh;
        public common::PbrMaterial* material;
        public uint drawBatch;

        public property float4x4 modelMatrix
        {
            get { return float4x4(model, float4(0.0, 0.0, 0.0, 1.0)); }
        }

        public property float3x3 normalMatrix
        {
            get { return float3x3(normalRow0, normalRow1, normalRow2); }
        }
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
