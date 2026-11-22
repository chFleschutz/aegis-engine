struct VSOut
{
    float4 pos : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
func vertexMain(in uint vertexID : SV_VertexID, out VSOut output)
{
    const float2 vertices[3] = {
        float2( 0.0,  0.5),
        float2( 0.5, -0.5),
        float2(-0.5, -0.5)
    };

    const float3 colors[3] = {
        float3(1.0, 0.0, 0.0), // Red
        float3(0.0, 1.0, 0.0), // Green
        float3(0.0, 0.0, 1.0)  // Blue
    };

    output.pos = float4(vertices[vertexID], 0.0, 1.0);
    output.color = colors[vertexID];
}


[shader("fragment")]
func fragmentMain(VSOut input, out float4 color : SV_Target)
{
    color = float4(input.color, 1.0);
}

// Buffer-device-address round trip: both arrays are reached through pointers passed as push
// constants, the shader-interface convention (see shaders/modules/README.md).
struct RoundTripConstants
{
    uint* src;
    uint* dst;
    uint count;
};

[[vk::push_constant]] ConstantBuffer<RoundTripConstants> push;

[shader("compute")]
[numthreads(64, 1, 1)]
func computeMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.count)
        return;
    push.dst[id.x] = push.src[id.x] * 3 + 1;
}
