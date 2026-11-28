import modules.bindless;

// Temporal anti-aliasing: blends this frame's jittered "SceneColor" with the previous frame's "TemporalColor",
// reprojected by the motion of the closest surface around the pixel and clipped to the current neighbourhood, into
// this frame's "TemporalColor" (render_graph::TemporalAaPass).

struct PushConstants
{
    bindless::StorageImageDescriptor output; // This frame's "TemporalColor"
    bindless::StorageImageDescriptor sceneColor;
    bindless::SampledImageDescriptor depth;
    bindless::SampledImageDescriptor motion; // "MotionVectors": uv moved since the last frame
    bindless::TextureDescriptor history;     // The previous frame's "TemporalColor", bilinear and clamped
    float historyWeight;                     // 0: the current frame alone (fresh history, a camera cut, disabled)
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func rgbToYCoCg(float3 color) -> float3
{
    return float3(
        dot(color, float3(0.25, 0.5, 0.25)),
        dot(color, float3(0.5, 0.0, -0.5)),
        dot(color, float3(-0.25, 0.5, -0.25)));
}

func yCoCgToRgb(float3 color) -> float3
{
    return float3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

func luminance(float3 color) -> float
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// The history at 'uv' through a Catmull-Rom filter, from nine bilinear taps of which the four corners are dropped
// (their weights are small): sharper than bilinear, so the accumulated image doesn't blur over time.
func sampleCatmullRom(float2 uv, float2 size) -> float3
{
    let position = uv * size;
    let centre = floor(position - 0.5) + 0.5;
    let f = position - centre;

    let w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    let w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    let w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    let w3 = f * f * (-0.5 + 0.5 * f);
    // The middle two taps as one bilinear fetch between them.
    let w12 = w1 + w2;
    let offset12 = w2 / w12;

    let texel = 1.0 / size;
    let uv0 = (centre - 1.0) * texel;
    let uv3 = (centre + 2.0) * texel;
    let uv12 = (centre + offset12) * texel;

    float3 result = push.history.sampleLevel(float2(uv12.x, uv0.y), 0.0).rgb * (w12.x * w0.y);
    result += push.history.sampleLevel(float2(uv0.x, uv12.y), 0.0).rgb * (w0.x * w12.y);
    result += push.history.sampleLevel(float2(uv12.x, uv12.y), 0.0).rgb * (w12.x * w12.y);
    result += push.history.sampleLevel(float2(uv3.x, uv12.y), 0.0).rgb * (w3.x * w12.y);
    result += push.history.sampleLevel(float2(uv12.x, uv3.y), 0.0).rgb * (w12.x * w3.y);
    let weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    // The negative lobes can overshoot below zero next to a bright texel.
    return max(result / weight, float3(0.0));
}

// 'history' moved towards the box's centre until it lies inside the box (clipping, not clamping, keeps its hue).
func clipToBox(float3 history, float3 boxMin, float3 boxMax) -> float3
{
    let centre = 0.5 * (boxMax + boxMin);
    let extent = max(0.5 * (boxMax - boxMin), float3(1e-4));
    let offset = history - centre;
    let units = abs(offset / extent);
    let largest = max(units.x, max(units.y, units.z));
    return largest > 1.0 ? centre + offset / largest : history;
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let output = bindless::storage2D(push.output);
    uint2 size;
    output.GetDimensions(size.x, size.y);
    if (any(dispatchID.xy >= size))
        return;

    let sceneColor = bindless::storage2D(push.sceneColor);
    let depth = bindless::texture2D(push.depth);
    let pixel = int2(dispatchID.xy);
    let current = sceneColor[dispatchID.xy].rgb;

    // The 3x3 neighbourhood: its colour moments in YCoCg, and the texel of the closest surface (depth is 0 near).
    float3 moment1 = float3(0.0);
    float3 moment2 = float3(0.0);
    int2 closest = pixel;
    float closestDepth = 2.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            let texel = clamp(pixel + int2(x, y), int2(0), int2(size) - 1);
            let color = rgbToYCoCg(sceneColor[uint2(texel)].rgb);
            moment1 += color;
            moment2 += color * color;
            let texelDepth = depth.Load(int3(texel, 0)).r;
            if (texelDepth < closestDepth)
            {
                closestDepth = texelDepth;
                closest = texel;
            }
        }
    }

    let uv = (float2(pixel) + 0.5) / float2(size);
    let motion = bindless::texture2D(push.motion).Load(int3(closest, 0)).xy;
    let previousUv = uv - motion;
    // Off-screen history is gone: the current frame alone.
    if (push.historyWeight <= 0.0 || any(previousUv < 0.0) || any(previousUv > 1.0))
    {
        output[dispatchID.xy] = float4(current, 1.0);
        return;
    }

    // Variance clipping: history further from the neighbourhood's mean than its spread belongs to another surface.
    let mean = moment1 / 9.0;
    let deviation = sqrt(abs(moment2 / 9.0 - mean * mean));
    let historyYCoCg = clipToBox(rgbToYCoCg(sampleCatmullRom(previousUv, float2(size))), mean - deviation, mean + deviation);
    let history = max(yCoCgToRgb(historyYCoCg), float3(0.0));

    // Weighted by inverse luminance, so a bright sample (a firefly) can't dominate the blend and smear.
    let currentWeight = (1.0 - push.historyWeight) / (1.0 + luminance(current));
    let historyWeight = push.historyWeight / (1.0 + luminance(history));
    let resolved = (current * currentWeight + history * historyWeight) / (currentWeight + historyWeight);
    output[dispatchID.xy] = float4(resolved, 1.0);
}
