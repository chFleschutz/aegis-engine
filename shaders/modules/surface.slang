module surface;

import modules.common;
import modules.tbn;

// The interface between a technique and a surface shader (the material template's shader code, in shaders/surfaces/).
// A technique file imports one surface module through SURFACE_MODULE, which the build defines per
// (technique, surface shader) pair (aegis_add_shaders TECHNIQUES/SURFACES); each surface module exports
// `typealias Surface` for its ISurface type, so techniques name only `Surface`. A technique never names the
// surface's parameter block: it passes the material's MaterialAddress to evaluateSurface<Surface>.

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

/// Where a material's parameter block is: the device address the renderer writes for the material. Its address is
/// visible only in this module, so a technique can't read the block as some other type: evaluateSurface reads it as
/// the Params of the surface shader it evaluates, the one the pipeline was built for.
public struct MaterialAddress
{
    uint64_t address;
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

/// Evaluates surface shader S at a surface point of a material that S's template lays out. The one place a material's
/// address becomes a typed pointer: its type is S.Params, bound to S.evaluate by the interface.
public func evaluateSurface<S : ISurface>(MaterialAddress material, SurfaceInput input) -> SurfaceOutput
{
    return S.evaluate((S.Params*)material.address, input);
}

/// The G-buffer texels of a surface point that surface shader S evaluated: its tangent-space normal in world space,
/// and S's shading model in ARM w.
public func toGBuffer<S : ISurface>(SurfaceInput input, SurfaceOutput surface) -> common::GBuffer
{
    let tbn = TBN::calcMatrix(input.worldPosition, input.worldNormal, input.uv);
    float3 N = normalize(length(surface.normal) < 0.1 ? input.worldNormal : mul(surface.normal, tbn));

    common::GBuffer output;
    output.position = float4(input.worldPosition, 1.0);
    output.normal = float4(N, 0.0);
    output.albedo = float4(surface.albedo, 1.0);
    output.arm = float4(surface.ambientOcclusion, surface.roughness, surface.metallic, float(uint(S.shadingModel())));
    output.emissive = float4(surface.emissive, 1.0);
    return output;
}
