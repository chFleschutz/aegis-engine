module lighting;

import modules.bindless;
import modules.constants;
import modules.pbr;
import modules.surface;

// The shading of a surface point by a view's lights: the ambient light (the sky's image-based lighting), the
// directional light, shadowed by its cascades' shadow maps, and the point and spot lights, the shadowed spot lights
// through their tiles of the spot shadow atlas. The lighting pass
// (lighting.slang) shades its G-buffer pixels with it; a pass that lights surfaces itself calls lighting::shade too, so
// every pass shades alike.

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

    /// render_graph::LightData: a point or spot light; a point light's cone lets everything through. A shadowed spot
    /// light carries its shadow view's matrix and its tile of the spot shadow atlas.
    public struct Light
    {
        public float3 position;
        public float range;
        public float3 color;
        public float intensity;
        public float3 direction; // Where a spot light's cone points
        public float coneScale;
        public float coneOffset;
        public float shadowBias;            // Texels of its tile a shaded point moves along its normal
        public float shadowTexelSize;       // Meters a texel of its tile spans per meter along its axis
        public float4 shadowRegion;         // Its tile in the atlas's uv: offset xy, size zw; zero: unshadowed
        public float4x4 shadowViewProjection;
    }

    /// render_graph::ShadowCascadeData: one cascade of the directional light's shadow, the depth of one of its shadow
    /// views (render_graph::fitDirectionalShadow).
    public struct ShadowCascade
    {
        public float4x4 viewProjection;
        public bindless::TextureDescriptor map; // With a comparison sampler
        public float texelSize;                 // Meters a texel spans
        public float splitFar;                  // Meters along the camera's axis where the cascade ends
    }

    /// render_graph::maxShadowCascades
    public static const uint MAX_SHADOW_CASCADES = 4;

    /// The directional light's shadow: its cascades, each covering the camera's axis up to its splitFar.
    public struct DirectionalShadow
    {
        public float4 cameraForward; // The axis the cascades split along
        public ShadowCascade cascades[MAX_SHADOW_CASCADES];
        public float bias;           // Texels of its cascade a shaded point moves along its normal
        public float blend;          // The fraction at the far end of each cascade blended into the next
        public uint cascadeCount;    // 0: the directional light is unshadowed
    }

    /// Where a point falls among the cascades: the first that covers it, and how far it is blended into the next
    /// one (0 to 1; the last cascade blends into no shadow). Past the last, cascade is the cascade count.
    public struct CascadeSelection
    {
        public uint cascade;
        public float blend;
    }

    /// The maps render_graph::Ibl bakes from the sky, which the ambient light reflects.
    public struct Environment
    {
        public bindless::TextureDescriptor irradiance;  // Cube
        public bindless::TextureDescriptor prefiltered; // Cube
        public bindless::TextureDescriptor brdfLut;
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
        public DirectionalShadow shadow;
        public bindless::TextureDescriptor spotShadowAtlas; // With a comparison sampler
        public uint spotShadows;                            // 0: the spot lights are unshadowed
        public Environment environment;

        public property Light* lights
        {
            get { return (Light*)lightAddress; }
        }
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

    func ambientLight(Lighting* lighting, float3 N, float3 V, float3 albedo, float ao,
                      float roughness, float metallic, float3 F0) -> float3
    {
        let environment = lighting->environment;
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

    /// The cascade of the directional light's shadow that covers 'position', by its distance along the camera's axis.
    public func shadowCascade(Lighting* lighting, float3 position) -> CascadeSelection
    {
        let count = lighting->shadow.cascadeCount;
        float depth = dot(position - lighting->cameraPosition.xyz, lighting->shadow.cameraForward.xyz);
        CascadeSelection selection;
        selection.cascade = 0;
        selection.blend = 0.0;
        while (selection.cascade < count && depth > lighting->shadow.cascades[selection.cascade].splitFar)
            selection.cascade++;
        if (selection.cascade == count)
            return selection;

        float near = selection.cascade == 0 ? 0.0 : lighting->shadow.cascades[selection.cascade - 1].splitFar;
        float far = lighting->shadow.cascades[selection.cascade].splitFar;
        float band = max((far - near) * lighting->shadow.blend, constants::EPSILON);
        selection.blend = saturate((depth - (far - band)) / band);
        return selection;
    }

    /// How much of the directional light reaches 'position' through one cascade's map: 3x3 comparison taps, each a
    /// bilinear blend of four texels (PCF). The point moves along its normal by the light's bias in the cascade's
    /// texels, more the more it faces away from the light, so a surface doesn't shadow itself (acne). Outside the map,
    /// it is lit.
    func cascadeShadow(Lighting* lighting, uint index, float3 position, float3 N) -> float
    {
        let cascade = lighting->shadow.cascades[index];
        float3 L = normalize(lighting->directional.direction.xyz);
        float NdotL = saturate(dot(N, L));
        float offset = lighting->shadow.bias * cascade.texelSize * (1.0 + 2.0 * (1.0 - NdotL));
        float4 clip = mul(cascade.viewProjection, float4(position + N * offset, 1.0));
        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * 0.5 + 0.5;
        if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
            return 1.0;

        let map = bindless::texture2D(cascade.map.image);
        let comparison = bindless::comparisonSampler(cascade.map.sampler);
        uint2 size;
        map.GetDimensions(size.x, size.y);
        float2 texel = 1.0 / float2(size);
        float lit = 0.0;
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
                lit += map.SampleCmpLevelZero(comparison, uv + float2(x, y) * texel, ndc.z);
        }
        return lit / 9.0;
    }

    /// How much of the directional light reaches 'position' (1: all, 0: none), from the cascade that covers it,
    /// blended into the next one near its far end. Past the last cascade, it is lit.
    public func directionalShadow(Lighting* lighting, float3 position, float3 N) -> float
    {
        let selection = shadowCascade(lighting, position);
        let count = lighting->shadow.cascadeCount;
        if (selection.cascade >= count)
            return 1.0;

        float lit = cascadeShadow(lighting, selection.cascade, position, N);
        if (selection.blend > 0.0)
        {
            float next = selection.cascade + 1 < count ? cascadeShadow(lighting, selection.cascade + 1, position, N) : 1.0;
            lit = lerp(lit, next, selection.blend);
        }
        return lit;
    }

    /// How much of a spot light reaches 'position' through its tile of the spot shadow atlas: 3x3 comparison taps, kept
    /// inside the tile so its neighbours don't bleed in. The point moves along its normal by the light's bias in texels of
    /// the tile at its distance, more the more it faces away from the light. Outside the light's view, or for a light
    /// without a tile, it is lit.
    func spotShadow(Lighting* lighting, Light light, float3 position, float3 N, float3 L) -> float
    {
        if (lighting->spotShadows == 0 || light.shadowRegion.z <= 0.0)
            return 1.0;

        float depth = max(dot(position - light.position, light.direction), 0.0);
        float NdotL = saturate(dot(N, L));
        float offset = light.shadowBias * light.shadowTexelSize * depth * (1.0 + 2.0 * (1.0 - NdotL));
        float4 clip = mul(light.shadowViewProjection, float4(position + N * offset, 1.0));
        if (clip.w <= 0.0)
            return 1.0;
        float3 ndc = clip.xyz / clip.w;
        float2 local = ndc.xy * 0.5 + 0.5;
        if (any(local < 0.0) || any(local > 1.0) || ndc.z > 1.0)
            return 1.0;

        let map = bindless::texture2D(lighting->spotShadowAtlas.image);
        let comparison = bindless::comparisonSampler(lighting->spotShadowAtlas.sampler);
        uint2 size;
        map.GetDimensions(size.x, size.y);
        float2 texel = 1.0 / float2(size);
        // A bilinear tap reads half a texel around its center: keep the taps' centers 1.5 texels inside the tile.
        float2 lowest = light.shadowRegion.xy + 1.5 * texel;
        float2 highest = light.shadowRegion.xy + light.shadowRegion.zw - 1.5 * texel;
        float2 uv = light.shadowRegion.xy + local * light.shadowRegion.zw;
        float lit = 0.0;
        for (int y = -1; y <= 1; y++)
        {
            for (int x = -1; x <= 1; x++)
                lit += map.SampleCmpLevelZero(comparison, clamp(uv + float2(x, y) * texel, lowest, highest), ndc.z);
        }
        return lit / 9.0;
    }

    func directionalLight(Lighting* lighting, float3 position, float3 N, float3 V, float3 albedo, float roughness,
                          float metallic, float3 F0) -> float3
    {
        float3 L = normalize(lighting->directional.direction.xyz);
        float3 radiance = lighting->directional.color.rgb * lighting->directional.color.w;
        if (dot(N, L) <= 0.0)
            return float3(0.0);
        radiance *= directionalShadow(lighting, position, N);
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
            if (attenuation <= 0.0)
                continue;
            attenuation *= spotShadow(lighting, light, position, N, L);
            float3 radiance = light.color * light.intensity * attenuation;
            Lo += PBR::computeLighting(N, V, L, albedo, roughness, metallic, radiance, F0);
        }
        return Lo;
    }

    /// The color 'point' leaves towards the view's camera: its emissive plus every light it reflects. An unlit point's
    /// color is its emissive, as it is.
    public func shade(Lighting* lighting, SurfacePoint point) -> float3
    {
        if (point.shadingModel == ShadingModel::Unlit)
            return max(point.emissive, 0.0);

        float ao = ambientOcclusion(lighting, point.ambientOcclusion);
        float3 N = normalize(point.normal);
        float3 V = normalize(lighting->cameraPosition.xyz - point.position);

        // Tint reflections for metallic surfaces
        float3 F0 = lerp(float3(PBR::F_DIELECTRIC), point.albedo, point.metallic);

        float3 Lo = point.emissive * EMISSIVE_INTENSITY;
        Lo += ambientLight(lighting, N, V, point.albedo, ao, point.roughness, point.metallic, F0);
        Lo += directionalLight(lighting, point.position, N, V, point.albedo, point.roughness, point.metallic, F0);
        Lo += punctualLights(lighting, point.position, N, V, point.albedo, point.roughness, point.metallic, F0);
        return max(Lo, 0.0);
    }
}
