import modules.bindless;
import modules.constants;

// Bloom, step 1: keeps the parts of the scene brighter than the threshold (soft knee), into mip 0.

struct PushConstants
{
    bindless::StorageImageDescriptor input;  // Scene color
    bindless::StorageImageDescriptor output; // Bloom mip 0
    float threshold;
    float softThreshold;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let output = bindless::storage2D(push.output);
    uint2 size;
    output.GetDimensions(size.x, size.y);
    if (any(dispatchID.xy >= size))
        return;

    float3 color = bindless::storage2D(push.input)[dispatchID.xy].rgb;

    // From: https://catlikecoding.com/unity/tutorials/advanced-rendering/bloom/#3.4
    float brightness = max(max(color.r, color.g), color.b);
    float knee = push.threshold * push.softThreshold;
    float soft = brightness - push.threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / max(4.0 * knee, constants::EPSILON);
    float contribution = max(soft, brightness - push.threshold);
    contribution /= max(brightness, constants::EPSILON);

    output[dispatchID.xy] = float4(color * contribution, 1.0);
}
