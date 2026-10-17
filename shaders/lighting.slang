import modules.bindless;
import modules.pbr;

// Deferred PBR lighting: reads the G-buffer, writes HDR scene color.

struct AmbientLight
{
    float4 color; // rgb, intensity
}

struct DirectionalLight
{
    float4 direction;
    float4 color; // rgb, intensity
}

struct PointLight
{
    float4 position;
    float4 color; // rgb, intensity
}

static const uint MAX_POINT_LIGHTS = 128;

/// render_graph::LightingPass::Uniforms
struct Lighting
{
    float4 cameraPosition;
    AmbientLight ambient;
    DirectionalLight directional;
    PointLight pointLights[MAX_POINT_LIGHTS];
    int pointLightCount;
    float ambientOcclusionFactor;
    int viewMode;
}

struct PushConstants
{
    Lighting* lighting;
    bindless::StorageImageDescriptor sceneColor;
    bindless::StorageImageDescriptor position;
    bindless::StorageImageDescriptor normal;
    bindless::StorageImageDescriptor albedo;
    bindless::StorageImageDescriptor arm;
    bindless::StorageImageDescriptor emissive;
    bindless::TextureDescriptor irradiance;  // Cube
    bindless::TextureDescriptor prefiltered; // Cube
    bindless::TextureDescriptor brdfLut;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const float EMISSIVE_INTENSITY = 2.0;
static const float MAX_REFLECTION_LOD = 4.0;

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    let sceneColorMap = bindless::storage2D(pc.sceneColor);
    uint2 outputSize;
    sceneColorMap.GetDimensions(outputSize.x, outputSize.y);
    if (any(dispatchThreadID.xy >= outputSize))
        return;

    let lighting = pc.lighting;
    int2 pixel = int2(dispatchThreadID.xy);

    float3 position = bindless::storage2D(pc.position)[pixel].xyz;
    float3 normal = bindless::storage2D(pc.normal)[pixel].xyz;
    float3 albedo = bindless::storage2D(pc.albedo)[pixel].xyz;
    float3 arm = bindless::storage2D(pc.arm)[pixel].xyz;
    float3 emissive = bindless::storage2D(pc.emissive)[pixel].xyz;

    // The AO factor fades the material's ambient occlusion: 0 turns it off, 1 (default) applies it fully
    float ao = lerp(1.0, arm.x, lighting->ambientOcclusionFactor);
    float roughness = arm.y;
    float metallic = arm.z;

    float3 N = normalize(normal);
    float3 V = normalize(lighting->cameraPosition.xyz - position);
    float3 R = reflect(-V, N);

    float NdotV = max(dot(N, V), 0.0);

    // Tint reflections for metallic surfaces
    float3 F0 = lerp(float3(PBR::F_DIELECTRIC), albedo, metallic);

    float3 Lo = emissive * EMISSIVE_INTENSITY;

    // Ambient Light
    {
        float3 F = PBR::specularReflection(NdotV, F0, roughness);
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);

        float3 diffuse = albedo * pc.irradiance.sampleCubeLevel(N, 0).rgb;

        float3 prefilter = pc.prefiltered.sampleCubeLevel(R, roughness * MAX_REFLECTION_LOD).rgb;
        float2 brdf = pc.brdfLut.sampleLevel(float2(NdotV, roughness), 0).rg;
        float3 specular = prefilter * (kS * brdf.x + brdf.y);

        float3 ambient = (kD * diffuse + specular) * ao;
        Lo += ambient * lighting->ambient.color.rgb * lighting->ambient.color.w;
    }
    // Directional Light
    {
        float3 L = normalize(lighting->directional.direction.xyz);
        float3 radiance = lighting->directional.color.rgb * lighting->directional.color.w;
        Lo += PBR::computeLighting(N, V, L, albedo, roughness, metallic, radiance, F0);
    }
    // Point Lights
    for (int i = 0; i < lighting->pointLightCount; i++)
    {
        PointLight light = lighting->pointLights[i];
        float3 L = normalize(light.position.xyz - position);

        float attenuation = PBR::lightAttenuation(light.position.xyz, position);
        float3 radiance = light.color.rgb * light.color.w * attenuation;
        Lo += PBR::computeLighting(N, V, L, albedo, roughness, metallic, radiance, F0);
    }

    float3 color = max(Lo, 0.0);
    switch (lighting->viewMode)
    {
    case 1: color = albedo; break;
    case 2: color = float3(ao); break;
    case 3: color = float3(roughness); break;
    case 4: color = float3(metallic); break;
    case 5: color = emissive; break;
    default: break;
    }
    sceneColorMap[pixel] = float4(color, 1.0);
}
