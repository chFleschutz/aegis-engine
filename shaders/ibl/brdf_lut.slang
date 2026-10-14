import modules.bindless;
import modules.constants;
import modules.ibl;

// Split-sum BRDF lookup table: scale and bias to F0 by (NdotV, roughness).

static const uint SAMPLE_COUNT = 1024;

struct PushConstants
{
    bindless::StorageImageDescriptor lut; // RG16F
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func importanceSampleGGX(float2 Xi, float3 N, float roughness) -> float3
{
    float a = roughness * roughness;

    float phi = 2.0 * constants::PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // from spherical coordinates to cartesian coordinates - halfway vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // from tangent-space H vector to world-space sample vector
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

func geometrySchlickGGX(float NdotV, float roughness) -> float
{
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / max(NdotV * (1.0 - k) + k, constants::EPSILON);
}

func geometrySmith(float NdotV, float NdotL, float roughness) -> float
{
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

func integrateBRDF(float NdotV, float roughness) -> float2
{
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;
    float3 N = float3(0.0, 0.0, 1.0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = ibl::hammersley(i, SAMPLE_COUNT);
        float3 H = importanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float G = geometrySmith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / max(NdotH * NdotV, constants::EPSILON);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let lut = bindless::storage2D(push.lut);
    uint2 imageSize;
    lut.GetDimensions(imageSize.x, imageSize.y);
    if (any(dispatchID.xy >= imageSize))
        return;

    float2 uv = float2(dispatchID.xy) / float2(imageSize);
    lut[dispatchID.xy] = float4(integrateBRDF(uv.x, uv.y), 0.0, 0.0);
}
