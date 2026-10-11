import rhi.modules.bindless;

// Draws the environment cube behind everything: depth is forced to 1 and tested less-or-equal
// against the G-buffer depth, so only pixels no geometry covered receive the sky.

struct PushConstants
{
    float4x4 viewProjection; // Rotation only: the sky never moves with the camera
    bindless::TextureDescriptor skybox;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

// A unit cube, 12 triangles wound to face inwards (front faces culled).
static const float3 CORNERS[8] = {
    float3(-1.0, -1.0, -1.0), float3( 1.0, -1.0, -1.0), float3( 1.0,  1.0, -1.0), float3(-1.0,  1.0, -1.0),
    float3(-1.0, -1.0,  1.0), float3( 1.0, -1.0,  1.0), float3( 1.0,  1.0,  1.0), float3(-1.0,  1.0,  1.0),
};
static const uint INDICES[36] = {
    0, 3, 2, 2, 1, 0,
    1, 2, 5, 5, 2, 6,
    4, 5, 6, 6, 7, 4,
    0, 4, 7, 7, 3, 0,
    2, 3, 7, 7, 6, 2,
    5, 4, 0, 0, 1, 5,
};

struct VSOut
{
    float4 position : SV_Position;
    [vk::location(0)] float3 localPos;
}

[shader("vertex")]
func vertexMain(uint vertexID : SV_VertexID, out VSOut output)
{
    let position = CORNERS[INDICES[vertexID]];
    float4 clipPos = mul(push.viewProjection, float4(position, 1.0));
    output.position = clipPos.xyww; // z = w: maximum depth
    output.localPos = position;
}

[shader("fragment")]
func fragmentMain(VSOut input, out float4 color : SV_Target)
{
    color = float4(push.skybox.sampleCube(input.localPos).rgb, 1.0);
}
