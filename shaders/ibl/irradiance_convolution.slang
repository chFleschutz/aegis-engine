import modules.bindless;
import modules.constants;
import modules.ibl;

// Diffuse irradiance: the environment convolved with a cosine lobe, per cube face texel.

static const uint SAMPLE_COUNT = 16 * 1024;
static const float3 MAX_BRIGHTNESS = float3(1000.0);

struct PushConstants
{
    bindless::TextureDescriptor environment; // Cube
    bindless::StorageImageDescriptor irradiance; // A 2D-array view of the irradiance cube
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func computeBasisVectors(const float3 N, out float3 S, out float3 T)
{
    // Branchless non-degenerate tangent
    T = cross(N, float3(0.0, 1.0, 0.0));
    T = lerp(cross(N, float3(1.0, 0.0, 0.0)), T, step(constants::EPSILON, dot(T, T)));
    T = normalize(T);
    S = cross(N, T);
}

func sampleHemisphere(float2 x) -> float3
{
    // Cosine weighted importance sampling
    float phi = 2.0 * constants::PI * x.y;
    float cosTheta = x.x;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

func hemisphereToWorld(const float3 v, const float3 N, const float3 S, const float3 T) -> float3
{
    return v.x * S + v.y * T + v.z * N;
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let irradianceMap = bindless::storage2DArray(push.irradiance);
    uint3 imageSize;
    irradianceMap.GetDimensions(imageSize.x, imageSize.y, imageSize.z);
    if (any(dispatchID.xy >= imageSize.xy))
        return;

    float2 st = float2(dispatchID.xy) / float2(imageSize.xy);
    float2 uv = float2(st.x, 1.0 - st.y) * 2.0 - 1.0;

    float3 N = ibl::cubeSampleVector(uv, dispatchID.z);
    float3 S, T;
    computeBasisVectors(N, S, T);

    float3 irradiance = float3(0.0);
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 pos = ibl::hammersley(i, SAMPLE_COUNT);
        float3 L = hemisphereToWorld(sampleHemisphere(pos), N, S, T);
        float NdotL = max(dot(N, L), 0.0);

        // Clamp color to reduces noise from overly bright pixels
        float3 sampleColor = min(push.environment.sampleCubeLevel(L, 0).rgb, MAX_BRIGHTNESS);
        irradiance += 2.0 * sampleColor * NdotL;
    }
    irradiance /= float3(SAMPLE_COUNT);
    irradianceMap[dispatchID] = float4(irradiance, 1.0);
}
