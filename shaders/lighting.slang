import modules.bindless;
import modules.lighting;
import modules.surface;

// Deferred PBR lighting: reads the G-buffer, writes HDR scene color. The shading is lighting::shade
// (modules/lighting.slang).

struct PushConstants
{
    lighting::Lighting* uniforms; // LightingConstants::lighting
    bindless::StorageImageDescriptor sceneColor;
    bindless::StorageImageDescriptor position;
    bindless::StorageImageDescriptor normal;
    bindless::StorageImageDescriptor albedo;
    bindless::StorageImageDescriptor arm;
    bindless::StorageImageDescriptor emissive;
    lighting::Environment environment;
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

// The ShadowCascades view: each cascade in its own color (red, green, blue, yellow; grey past the last), blended as
// the shadow blends them, and darker where the directional light is shadowed.
func shadowCascadesView(float3 position, float3 normal) -> float3
{
    static const float3 colors[lighting::MAX_SHADOW_CASCADES + 1] = {
        float3(1.0, 0.25, 0.25), float3(0.25, 1.0, 0.25), float3(0.25, 0.25, 1.0), float3(1.0, 1.0, 0.25), float3(0.5)
    };
    let selection = lighting::shadowCascade(push.uniforms, position);
    let count = push.uniforms->shadow.cascadeCount;
    let next = selection.cascade + 1 < count ? selection.cascade + 1 : lighting::MAX_SHADOW_CASCADES;
    float3 tint = selection.cascade < count ? lerp(colors[selection.cascade], colors[next], selection.blend) : colors[lighting::MAX_SHADOW_CASCADES];
    return tint * (0.25 + 0.75 * lighting::directionalShadow(push.uniforms, position, normalize(normal)));
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    let sceneColorMap = bindless::storage2D(push.sceneColor);
    uint2 outputSize;
    sceneColorMap.GetDimensions(outputSize.x, outputSize.y);
    if (any(dispatchThreadID.xy >= outputSize))
        return;

    int2 pixel = int2(dispatchThreadID.xy);
    float4 arm = bindless::storage2D(push.arm)[pixel];

    lighting::SurfacePoint point;
    point.position = bindless::storage2D(push.position)[pixel].xyz;
    point.normal = bindless::storage2D(push.normal)[pixel].xyz;
    point.albedo = bindless::storage2D(push.albedo)[pixel].xyz;
    point.ambientOcclusion = arm.x;
    point.roughness = arm.y;
    point.metallic = arm.z;
    point.emissive = bindless::storage2D(push.emissive)[pixel].xyz;
    point.shadingModel = arm.w > 0.5 ? ShadingModel::Unlit : ShadingModel::Lit; // ARM's w (modules/surface.slang)

    float3 color = lighting::shade(push.uniforms, push.environment, point);
    switch (push.uniforms->viewMode)
    {
    case 1: color = point.albedo; break;
    case 2: color = float3(lighting::ambientOcclusion(push.uniforms, point.ambientOcclusion)); break;
    case 3: color = float3(point.roughness); break;
    case 4: color = float3(point.metallic); break;
    case 5: color = point.emissive; break;
    case 6: color = shadowCascadesView(point.position, point.normal); break;
    default: break;
    }
    sceneColorMap[pixel] = float4(color, 1.0);
}
