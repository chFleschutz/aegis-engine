import rhi.modules.bindless;

// HDR scene color (+ bloom) -> tone mapped, gamma corrected final image.

struct PushConstants
{
    bindless::StorageImageDescriptor final;
    bindless::StorageImageDescriptor sceneColor;
    bindless::StorageImageDescriptor bloom; // Without a bloom pass: sceneColor, with bloomIntensity 0
    int toneMappingMode;
    float bloomIntensity;
    float exposure;
    float gamma;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func toneMapReinhard(float3 color) -> float3
{
    return color / (color + float3(1.0));
}

func toneMapACES(float3 color) -> float3
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}

func applyToneMapping(float3 color) -> float3
{
    switch (push.toneMappingMode)
    {
    case 0: return toneMapReinhard(color);
    case 1: return toneMapACES(color);
    default: return color;
    }
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let finalMap = bindless::storage2D(push.final);
    uint2 size;
    finalMap.GetDimensions(size.x, size.y);
    if (any(dispatchID.xy >= size))
        return;

    float3 sceneColor = bindless::storage2D(push.sceneColor)[dispatchID.xy].rgb;
    float3 bloomColor = bindless::storage2D(push.bloom)[dispatchID.xy].rgb;

    float3 color = sceneColor + push.bloomIntensity * bloomColor;
    color = applyToneMapping(color * push.exposure);
    color = pow(color, float3(1.0 / push.gamma));
    finalMap[dispatchID.xy] = float4(color, 1.0);
}
