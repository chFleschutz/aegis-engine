module surface;

import modules.common;
import modules.tbn;

// The interface between a technique and a surface shader (the material template's shader code, in shaders/surfaces/).
// A technique file imports one surface module through SURFACE_MODULE, which the build defines per
// (technique, surface shader) pair (aegis_add_shaders TECHNIQUES/SURFACES); each surface module exports
// `typealias Surface` for its ISurface type, so techniques name only `Surface`.

/// How the lighting pass shades a G-buffer pixel, stored in the ARM target's w (0 = lit, 1 = unlit).
public enum class ShadingModel : uint
{
    Lit = 0,
    Unlit = 1,
}

/// What a technique knows about the surface point it shades.
public struct SurfaceInput
{
    public float2 uv;
    public float3 worldPosition;
    public float3 worldNormal;
}

/// The properties of a surface point.
public struct SurfaceOutput
{
    public float3 albedo;
    public float3 normal; // Tangent space; zero length uses the vertex normal
    public float ambientOcclusion;
    public float roughness;
    public float metallic;
    public float3 emissive;
    public float alpha;
}

public interface ISurface
{
    /// The material's parameter block, laid out by its template (scalar rules, see README.md). The template's
    /// parameter list must match it field for field: the build writes its layout (surfaces/<name>.layout.json) and
    /// the template loader checks it.
    associatedtype Params;

    static func shadingModel() -> ShadingModel;
    static func evaluate(Params* params, SurfaceInput input) -> SurfaceOutput;
}

/// The G-buffer texels of a surface point: its tangent-space normal in world space, and its shading model in ARM w.
public func toGBuffer(SurfaceInput input, SurfaceOutput surface, ShadingModel shadingModel) -> common::GBuffer
{
    let tbn = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(surface.normal) < 0.1 ? input.worldNormal : mul(surface.normal, tbn));

    common::GBuffer output;
    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(surface.albedo, 1.0);
    output.arm = float4(surface.ambientOcclusion, surface.roughness, surface.metallic, float(uint(shadingModel)));
    output.emissive = float4(surface.emissive, 1.0);
    return output;
}
