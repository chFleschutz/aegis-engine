import modules.bindless;
import modules.constants;
import modules.ibl;

// Specular prefiltering: one mip of the prefiltered cube per roughness, GGX importance sampled.

static const uint SAMPLE_COUNT = 4096;

struct PushConstants
{
    bindless::TextureDescriptor environment;   // Cube
    bindless::StorageImageDescriptor prefiltered; // A 2D-array view of one mip of the prefiltered cube
    float roughness;
    float envResolution; // Width of the environment cube's mip 0
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func importanceSampleGGX(float2 Xi, float3 N, float roughness) -> float3
{
    float alpha2 = roughness * roughness * roughness * roughness;

    float phi = 2.0 * constants::PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha2 - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to world space
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return tangent * H.x + bitangent * H.y + N * H.z;
}

func distributionGGX(float NdotH, float roughness) -> float
{
    float alpha2 = roughness * roughness * roughness * roughness;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (alpha2 - 1.0) + 1.0;
    return alpha2 / (constants::PI * denom * denom);
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let prefilteredMap = bindless::storage2DArray(push.prefiltered);
    uint3 imageSize;
    prefilteredMap.GetDimensions(imageSize.x, imageSize.y, imageSize.z);
    if (any(dispatchID.xy >= imageSize.xy))
        return;

    float2 st = float2(dispatchID.xy) / float2(imageSize.xy);
    float2 uv = float2(st.x, 1.0 - st.y) * 2.0 - 1.0;

    float3 N = ibl::cubeSampleVector(uv, dispatchID.z);
    float3 R = N;
    float3 V = R;

    // Prefilter environment map using hammersley quasi monte-carlo integration
    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0.0);
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = ibl::hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleGGX(Xi, N, push.roughness);
        float3 L = 2.0 * dot(V, H) * H - V;

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D = distributionGGX(NdotH, push.roughness);
            float pdf = D * NdotH / max(4.0 * HdotV, constants::EPSILON);

            float saTexel = 4.0 * constants::PI / (6.0 * push.envResolution * push.envResolution);
            float saSample = 1.0 / max(float(SAMPLE_COUNT) * pdf, constants::EPSILON);

            float mipLevel = push.roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            prefilteredColor += push.environment.sampleCubeLevel(L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / max(totalWeight, constants::EPSILON);
    prefilteredMap[dispatchID] = float4(prefilteredColor, 1.0);
}
