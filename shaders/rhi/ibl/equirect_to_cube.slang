import rhi.modules.bindless;
import rhi.modules.ibl;

// Projects an equirectangular (longitude/latitude) HDR image onto the six faces of a cube map.

static const float2 INV_ATAN = float2(0.1591, 0.3183);

struct PushConstants
{
    bindless::TextureDescriptor equirect;
    bindless::StorageImageDescriptor cube; // A 2D-array view of the cube's mip 0
}

[[vk::push_constant]] ConstantBuffer<PushConstants> push;

func sampleEquirectangular(float3 dir) -> float2
{
    // Note: Directions might be shuffled because of Z-up to Y-up conversion
    float2 uv = float2(atan2(dir.y, dir.x), asin(-dir.z));
    uv *= INV_ATAN;
    uv += 0.5;
    return uv;
}

[shader("compute")]
[numthreads(16, 16, 1)]
func main(uint3 dispatchID: SV_DispatchThreadID)
{
    let cubeMap = bindless::storage2DArray(push.cube);
    uint3 imageSize;
    cubeMap.GetDimensions(imageSize.x, imageSize.y, imageSize.z);
    if (any(dispatchID.xy >= imageSize.xy))
        return;

    float2 st = float2(dispatchID.xy) / float2(imageSize.xy);
    float2 uv = float2(st.x, 1.0 - st.y) * 2.0 - 1.0;

    float3 dir = ibl::cubeSampleVector(uv, dispatchID.z);
    cubeMap[dispatchID] = push.equirect.sampleLevel(sampleEquirectangular(dir), 0);
}
