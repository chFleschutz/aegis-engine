module lighting;

import modules.bindless;
import modules.constants;
import modules.pbr;
import modules.surface;

// The shading of a surface point by a view's lights: the ambient light (the sky's image-based lighting), the
// directional light and the point and spot lights. The lighting pass (lighting.slang) shades its G-buffer pixels
// with it; a pass that lights surfaces itself calls lighting::shade too, so every pass shades alike.

namespace lighting
{
    public struct AmbientLight
    {
        public float4 color; // rgb, intensity
    }

    public struct DirectionalLight
    {
        public float4 direction;
        public float4 color; // rgb, intensity
    }

    /// render_graph::LightData: a point or spot light; a point light's cone lets everything through.
    public struct Light
    {
        public float3 position;
        public float range;
        public float3 color;
        public float intensity;
        public float3 direction; // Where a spot light's cone points
        public float coneScale;
        public float coneOffset;
    }

    /// render_graph::LightingUniforms: a view's lights, one block per view and frame in flight.
    public struct Lighting
    {
        public float4 cameraPosition;
        public AmbientLight ambient;
        public DirectionalLight directional;
        uint64_t lightAddress; // Light*: the point lights, then the spot lights
        public uint pointLightCount;
        public uint spotLightCount;
        public float ambientOcclusionFactor;
        public int viewMode; // The lighting pass's debug view (render_graph::LightingViewMode)

        public property Light* lights
        {
            get { return (Light*)lightAddress; }
        }
    }

    /// The maps render_graph::Ibl bakes from the sky, which the ambient light reflects.
    public struct Environment
    {
        public bindless::TextureDescriptor irradiance;  // Cube
        public bindless::TextureDescriptor prefiltered; // Cube
        public bindless::TextureDescriptor brdfLut;
    }

    /// A surface point to shade, in world space.
    public struct SurfacePoint
    {
        public float3 position;
        public float3 normal; // Need not be normalized
        public float3 albedo;
        public float ambientOcclusion; // The material's; shade fades it by Lighting::ambientOcclusionFactor
        public float roughness;
        public float metallic;
        public float3 emissive;
        public ShadingModel shadingModel;
    }

    static const float EMISSIVE_INTENSITY = 2.0;
    /// The last mip of the prefiltered map, the roughest reflection (render_graph::maxReflectionLod).
    static const float MAX_REFLECTION_LOD = 4.0;

    /// The material's ambient occlusion faded by the view's AO factor: 0 turns it off, 1 (default) applies it fully.
    public func ambientOcclusion(Lighting* lighting, float materialOcclusion) -> float
    {
        return lerp(1.0, materialOcclusion, lighting->ambientOcclusionFactor);
    }

    func ambientLight(Lighting* lighting, Environment environment, float3 N, float3 V, float3 albedo, float ao,
                      float roughness, float metallic, float3 F0) -> float3
    {
        float3 R = reflect(-V, N);
        float NdotV = max(dot(N, V), 0.0);

        float3 F = PBR::specularReflection(NdotV, F0, roughness);
        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);

        float3 diffuse = albedo * environment.irradiance.sampleCubeLevel(N, 0).rgb;

        float3 prefilter = environment.prefiltered.sampleCubeLevel(R, roughness * MAX_REFLECTION_LOD).rgb;
        float2 brdf = environment.brdfLut.sampleLevel(float2(NdotV, roughness), 0).rg;
        float3 specular = prefilter * (kS * brdf.x + brdf.y);

        float3 ambient = (kD * diffuse + specular) * ao;
        return ambient * lighting->ambient.color.rgb * lighting->ambient.color.w;
    }

    func directionalLight(Lighting* lighting, float3 N, float3 V, float3 albedo, float roughness, float metallic,
                          float3 F0) -> float3
    {
        float3 L = normalize(lighting->directional.direction.xyz);
        float3 radiance = lighting->directional.color.rgb * lighting->directional.color.w;
        return PBR::computeLighting(N, V, L, albedo, roughness, metallic, radiance, F0);
    }

    // Point and spot lights: they shade alike
    func punctualLights(Lighting* lighting, float3 position, float3 N, float3 V, float3 albedo, float roughness,
                        float metallic, float3 F0) -> float3
    {
        float3 Lo = float3(0.0);
        let lights = lighting->lights;
        let lightCount = lighting->pointLightCount + lighting->spotLightCount;
        for (uint i = 0; i < lightCount; i++)
        {
            Light light = lights[i];
            float3 toLight = light.position - position;
            float distance = length(toLight);
            if (distance >= light.range)
                continue;
            float3 L = toLight / max(distance, constants::EPSILON);

            float attenuation = PBR::distanceAttenuation(distance, light.range) *
                                PBR::coneAttenuation(light.direction, light.coneScale, light.coneOffset, -L);
            float3 radiance = light.color * light.intensity * attenuation;
            Lo += PBR::computeLighting(N, V, L, albedo, roughness, metallic, radiance, F0);
        }
        return Lo;
    }

    /// The color 'point' leaves towards the view's camera: its emissive plus every light it reflects. An unlit point's
    /// color is its emissive, as it is.
    public func shade(Lighting* lighting, Environment environment, SurfacePoint point) -> float3
    {
        if (point.shadingModel == ShadingModel::Unlit)
            return max(point.emissive, 0.0);

        float ao = ambientOcclusion(lighting, point.ambientOcclusion);
        float3 N = normalize(point.normal);
        float3 V = normalize(lighting->cameraPosition.xyz - point.position);

        // Tint reflections for metallic surfaces
        float3 F0 = lerp(float3(PBR::F_DIELECTRIC), point.albedo, point.metallic);

        float3 Lo = point.emissive * EMISSIVE_INTENSITY;
        Lo += ambientLight(lighting, environment, N, V, point.albedo, ao, point.roughness, point.metallic, F0);
        Lo += directionalLight(lighting, N, V, point.albedo, point.roughness, point.metallic, F0);
        Lo += punctualLights(lighting, point.position, N, V, point.albedo, point.roughness, point.metallic, F0);
        return max(Lo, 0.0);
    }
}
