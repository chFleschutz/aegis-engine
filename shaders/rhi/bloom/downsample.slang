import rhi.modules.bindless;
import rhi.modules.constants;

// Bloom, step 2: each mip is a 13-tap filtered downsample of the one above.

struct PushConstants
{
    bindless::StorageImageDescriptor output; // Mip n + 1
    bindless::TextureDescriptor input;       // Mip n, clamped
    int mipLevel;                            // n
    float filterScale;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func toSRGB(float3 color) -> float3
{
    return pow(color, float3(constants::INV_GAMMA));
}

func luminance(float3 color) -> float
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

func karisAverage(float3 color) -> float3
{
    return 1.0 / (1.0 + luminance(toSRGB(color)) * 0.25);
}

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

    // Take 13 samples around the current texel (e)
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    let input = push.input;
    float3 a = input.sampleLevel(float2(uv.x - 2.0 * step.x, uv.y - 2.0 * step.y), 0).rgb;
    float3 b = input.sampleLevel(float2(uv.x,                uv.y - 2.0 * step.y), 0).rgb;
    float3 c = input.sampleLevel(float2(uv.x + 2.0 * step.x, uv.y - 2.0 * step.y), 0).rgb;
    float3 d = input.sampleLevel(float2(uv.x - 2.0 * step.x, uv.y), 0).rgb;
    float3 e = input.sampleLevel(float2(uv.x,                uv.y), 0).rgb;
    float3 f = input.sampleLevel(float2(uv.x + 2.0 * step.x, uv.y), 0).rgb;
    float3 g = input.sampleLevel(float2(uv.x - 2.0 * step.x, uv.y + 2.0 * step.y), 0).rgb;
    float3 h = input.sampleLevel(float2(uv.x,                uv.y + 2.0 * step.y), 0).rgb;
    float3 i = input.sampleLevel(float2(uv.x + 2.0 * step.x, uv.y + 2.0 * step.y), 0).rgb;
    float3 j = input.sampleLevel(float2(uv.x - step.x,       uv.y - step.y), 0).rgb;
    float3 k = input.sampleLevel(float2(uv.x + step.x,       uv.y - step.y), 0).rgb;
    float3 l = input.sampleLevel(float2(uv.x - step.x,       uv.y + step.y), 0).rgb;
    float3 m = input.sampleLevel(float2(uv.x + step.x,       uv.y + step.y), 0).rgb;

    // Apply weighted average
    //        | 1 - 2 - 1 |
    //        | - 4 - 4 - |
    // 1/32 * | 2 - 4 - 2 |
    //        | - 4 - 4 - |
    //        | 1 - 2 - 1 |
    float3 color = float3(0.0);
    if (push.mipLevel == 0)
    {
        // Karis average on the first downsample filters fireflies
        float3 group1 = (a + b + d + e) * (0.125 / 4.0);
        float3 group2 = (b + c + e + f) * (0.125 / 4.0);
        float3 group3 = (d + e + g + h) * (0.125 / 4.0);
        float3 group4 = (e + f + h + i) * (0.125 / 4.0);
        float3 group5 = (j + k + l + m) * (0.5 / 4.0);
        group1 *= karisAverage(group1);
        group2 *= karisAverage(group2);
        group3 *= karisAverage(group3);
        group4 *= karisAverage(group4);
        group5 *= karisAverage(group5);
        color = group1 + group2 + group3 + group4 + group5;
    }
    else
    {
        color = e * 0.125;
        color += (a + c + g + i) * 0.03125;
        color += (b + d + f + h) * 0.0625;
        color += (j + k + l + m) * 0.125;
    }

    color = max(color, constants::EPSILON);
    outputImage[dispatchID.xy] = float4(color, 1.0);
}
