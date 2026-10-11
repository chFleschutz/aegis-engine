import rhi.modules.common;
import rhi.modules.constants;

// Point lights as glowing billboards, alpha blended into the scene color after lighting.

static const float2 OFFSETS[6] = {
    float2(-1.0, -1.0),
    float2( 1.0, -1.0),
    float2(-1.0,  1.0),
    float2(-1.0,  1.0),
    float2( 1.0, -1.0),
    float2( 1.0,  1.0),
};

struct PushConstants
{
    float4 worldPosition;
    float4 color;
    float radius;
    common::Camera* camera;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSOut
{
    float4 position : SV_Position;
    [vk::location(0)] float2 fragOffset;
}

[shader("vertex")]
func vertexMain(uint vertexID : SV_VertexID, out VSOut output)
{
    output.fragOffset = OFFSETS[vertexID];
    float4 viewOffset = float4(output.fragOffset, 0.0, 0.0) * pc.radius;
    float4 viewPosition = mul(pc.camera->view, pc.worldPosition);
    output.position = mul(pc.camera->projection, viewPosition + viewOffset);
}

[shader("fragment")]
func fragmentMain(VSOut input, out float4 color : SV_Target)
{
    float offsetDistance = dot(input.fragOffset, input.fragOffset);
    if (offsetDistance > 1.0)
        discard;

    float cosDistance = (cos(offsetDistance * constants::PI) + 1.0) * 0.5;
    color = float4(pc.color.xyz + cosDistance, cosDistance);
}
