module pbr_surface;

import modules.bindless;
import modules.surface;

// The PBR material template's surface shader (content/pbr.template): metallic-roughness, lit.

/// The parameter block, in the order of pbr.template's parameters (assets::pbr names them in C++).
public struct PbrParams
{
    public float3 albedo;
    public float3 emissive;
    public float metallic;
    public float roughness;
    public float ambientOcclusion;
    public bindless::TextureDescriptor albedoMap;
    public bindless::TextureDescriptor normalMap;
    public bindless::TextureDescriptor metalRoughnessMap;
    public bindless::TextureDescriptor ambientOcclusionMap;
    public bindless::TextureDescriptor emissiveMap;
}

public struct PbrSurface : ISurface
{
    public typealias Params = PbrParams;

    public static func shadingModel() -> ShadingModel { return ShadingModel.Lit; }

    public static func evaluate(PbrParams* material, SurfaceInput input) -> SurfaceOutput
    {
        float2 metallicRoughness = material->metalRoughnessMap.sample(input.uv).bg;

        SurfaceOutput output;
        output.albedo = material->albedoMap.sample(input.uv).rgb * material->albedo;
        output.normal = material->normalMap.sample(input.uv).rgb * 2.0 - 1.0;
        output.ambientOcclusion = material->ambientOcclusionMap.sample(input.uv).r * material->ambientOcclusion;
        output.roughness = metallicRoughness.g * material->roughness;
        output.metallic = metallicRoughness.r * material->metallic;
        output.emissive = material->emissiveMap.sample(input.uv).rgb * material->emissive;
        output.alpha = 1.0;
        return output;
    }
}

public typealias Surface = PbrSurface;
