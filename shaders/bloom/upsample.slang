import modules.bindless;

// Bloom, step 3: each mip becomes a 3x3 tent-filtered upsample of the one below, back up to mip 0.

struct PushConstants
{
    bindless::StorageImageDescriptor output; // Mip n
    bindless::TextureDescriptor input;       // Mip n + 1, clamped
    float filterScale;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let outputImage = bindless::storage2D(push.output);
    uint2 outputSize;
    outputImage.GetDimensions(outputSize.x, outputSize.y);
    if (any(dispatchID.xy >= outputSize))
        return;

    float2 texelSize = 1.0 / float2(outputSize);
    float2 uv = (float2(dispatchID.xy) + 0.5) * texelSize;
    float2 step = texelSize * push.filterScale;

    // Take 9 samples around the current texel (e)
    // a - b - c
    // d - e - f
    // g - h - i
    let input = push.input;
    float3 a = input.sampleLevel(float2(uv.x - step.x, uv.y - step.y), 0).rgb;
    float3 b = input.sampleLevel(float2(uv.x,          uv.y - step.y), 0).rgb;
    float3 c = input.sampleLevel(float2(uv.x + step.x, uv.y - step.y), 0).rgb;
    float3 d = input.sampleLevel(float2(uv.x - step.x, uv.y), 0).rgb;
    float3 e = input.sampleLevel(float2(uv.x,          uv.y), 0).rgb;
    float3 f = input.sampleLevel(float2(uv.x + step.x, uv.y), 0).rgb;
    float3 g = input.sampleLevel(float2(uv.x - step.x, uv.y + step.y), 0).rgb;
    float3 h = input.sampleLevel(float2(uv.x,          uv.y + step.y), 0).rgb;
    float3 i = input.sampleLevel(float2(uv.x + step.x, uv.y + step.y), 0).rgb;

    // Apply 3x3 gaussian filter weights
    //        | 1 2 1 |
    // 1/16 * | 2 4 2 |
    //        | 1 2 1 |
    float3 bloomColor = (a + c + g + i) * 0.0625 + (b + d + f + h) * 0.125 + e * 0.25;
    outputImage[dispatchID.xy] = float4(bloomColor, 1.0);
}
