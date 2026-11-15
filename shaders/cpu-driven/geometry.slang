import modules.common;
import modules.surface;
import SURFACE_MODULE;

// CPU-driven G-buffer technique: one draw per entity, vertices pulled through the mesh's device address
// (the index buffer is bound, so SV_VertexID is the vertex index). Built once per surface shader, which
// SURFACE_MODULE names (see modules/surface.slang).

struct PushConstants
{
    float3x4 model; // Top three rows of the model matrix
    float3 normalRow0;
    float3 normalRow1;
    float3 normalRow2;
    common::Camera* camera;
    common::Mesh* mesh;
    Surface.Params* material;

    property float4x4 modelMatrix
    {
        get { return float4x4(model, float4(0.0, 0.0, 0.0, 1.0)); }
    }

    property float3x3 normalMatrix
    {
        get { return float3x3(normalRow0, normalRow1, normalRow2); }
    }
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

struct VSOut
{
    float4 position : SV_Position;
    [vk::location(0)] float3 worldPosition;
    [vk::location(1)] float3 worldNormal;
    [vk::location(2)] float2 uv;
}

[shader("vertex")]
func vertexMain(uint vertexID : SV_VertexID, out VSOut output)
{
    let vertex = push.mesh->vertices[vertexID];
    let camera = push.camera;

    float4 worldPosition = mul(push.modelMatrix, float4(vertex.position, 1.0));
    output.position = mul(camera->viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul(push.normalMatrix, vertex.normal));
    output.uv = vertex.uv;
}

[shader("fragment")]
func fragmentMain(in VSOut input, out common::GBuffer output)
{
    SurfaceInput surfaceInput;
    surfaceInput.uv = input.uv;
    surfaceInput.worldPosition = input.worldPosition;
    surfaceInput.worldNormal = input.worldNormal;

    let surface = Surface.evaluate(push.material, surfaceInput);
    output = toGBuffer(surfaceInput, surface, Surface.shadingModel());
}
