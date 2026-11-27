import modules.common;
import modules.surface;
import SURFACE_MODULE;

// CPU-driven G-buffer and depth techniques: one draw per entity, vertices pulled through the mesh's device address
// (the index buffer is bound, so SV_VertexID is the vertex index). Built once per surface shader, which
// SURFACE_MODULE names (see modules/surface.slang), and again with ALPHA_MASK for masked materials.

struct PushConstants
{
    float3x4 model; // Top three rows of the model matrix
    float3 normalRow0;
    float3 normalRow1;
    float3 normalRow2;
    common::Camera* camera;
    common::Mesh* mesh;
    MaterialAddress material;

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
    output.position = mul(camera->jitteredViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul(push.normalMatrix, vertex.normal));
    output.uv = vertex.uv;
}

[shader("fragment")]
func fragmentMain(in VSOut input, bool isFrontFace : SV_IsFrontFace, out common::GBuffer output)
{
    let surfaceInput = toSurfaceInput(input.uv, input.worldPosition, input.worldNormal, isFrontFace);
    let surface = evaluateSurface<Surface>(push.material, surfaceInput);
#if defined(ALPHA_MASK)
    alphaTest(surface);
#endif
    output = toGBuffer<Surface>(surfaceInput, surface);
}

#if defined(ALPHA_MASK)
// The depth technique's fragment stage (render_graph::Technique::Depth), as a shadow view draws: only the masked build
// has one, which evaluates the surface for its alpha test. Unmasked depth pipelines have no fragment stage.
[shader("fragment")]
func depthFragmentMain(in VSOut input, bool isFrontFace : SV_IsFrontFace)
{
    let surfaceInput = toSurfaceInput(input.uv, input.worldPosition, input.worldNormal, isFrontFace);
    alphaTest(evaluateSurface<Surface>(push.material, surfaceInput));
}
#endif
