module ibl;

namespace ibl
{
    public func cubeSampleVector(const float2 uv, uint face) -> float3
    {
        float3 dir;
        if (face == 0) dir = float3(1.0, uv.y, -uv.x);
        else if (face == 1) dir = float3(-1.0, uv.y, uv.x);
        else if (face == 2) dir = float3(uv.x, 1.0, -uv.y);
        else if (face == 3) dir = float3(uv.x, -1.0, uv.y);
        else if (face == 4) dir = float3(uv.x, uv.y, 1.0);
        else if (face == 5) dir = float3(-uv.x, uv.y, -1.0);
        return normalize(dir);
    }

    public func radicalInverse(uint bits) -> float
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return float(bits) * 2.3283064365386963e-10;
    }

    public func hammersley(uint i, uint sampleCount) -> float2
    {
        return float2(float(i) / float(sampleCount), radicalInverse(i));
    }
}