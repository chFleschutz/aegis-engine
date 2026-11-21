import modules.common;
import modules.lighting;
import modules.surface;
import SURFACE_MODULE;

// CPU-driven forward technique (render_graph::Technique::Forward): draws a blended material lit by the view's lights,
// as the lighting pass lights the G-buffer (lighting::shade), over the scene color with its alpha. One draw per entity,
// vertices pulled as in geometry.slang. Built once per surface shader, which SURFACE_MODULE names (see
// modules/surface.slang); its masked build (ALPHA_MASK) is never drawn, since a blended material is not masked.

struct PushConstants
{
    float3x4 model; // Top three rows of the model matrix
    float3 normalRow0;
    float3 normalRow1;
    float3 normalRow2;
    common::Camera* camera;
    common::Mesh* mesh;
    MaterialAddress material;
    lighting::Lighting* lighting; // The view's, which its lighting pass wrote this frame

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
func fragmentMain(in VSOut input, bool isFrontFace : SV_IsFrontFace, out float4 color : SV_Target)
{
    let surfaceInput = toSurfaceInput(input.uv, input.worldPosition, input.worldNormal, isFrontFace);
    let surface = evaluateSurface<Surface>(push.material, surfaceInput);

    lighting::SurfacePoint point;
    point.position = input.worldPosition;
    point.normal = worldNormal(surfaceInput, surface);
    point.albedo = surface.albedo;
    point.ambientOcclusion = surface.ambientOcclusion;
    point.roughness = surface.roughness;
    point.metallic = surface.metallic;
    point.emissive = surface.emissive;
    point.shadingModel = Surface.shadingModel();
    color = float4(lighting::shade(push.lighting, point), saturate(surface.alpha));
}
