module unlit_surface;

import modules.bindless;
import modules.surface;

// The unlit material template's surface shader (content/unlit.template): a colour the lighting pass passes through.

/// The parameter block, in the order of unlit.template's parameters.
public struct UnlitParams
{
    public float3 color;
    public bindless::TextureDescriptor colorMap;
}

public struct UnlitSurface : ISurface
{
    public typealias Params = UnlitParams;

    public static func shadingModel() -> ShadingModel { return ShadingModel.Unlit; }

    public static func evaluate(UnlitParams* material, SurfaceInput input) -> SurfaceOutput
    {
        SurfaceOutput output;
        output.albedo = float3(0.0);
        output.normal = float3(0.0);
        output.ambientOcclusion = 0.0;
        output.roughness = 0.0;
        output.metallic = 0.0;
        output.emissive = material->colorMap.sample(input.uv).rgb * material->color;
        output.alpha = 1.0;
        return output;
    }
}

public typealias Surface = UnlitSurface;
