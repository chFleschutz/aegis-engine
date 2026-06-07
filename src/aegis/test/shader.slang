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
