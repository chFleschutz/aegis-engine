module TBN;

import modules.constants;

namespace TBN
{
    public func calcMatrix(float3 worldPos, float3 worldNormal, float2 uv) -> float3x3
    {
        float3 Q1 = ddx(worldPos);
        float3 Q2 = ddy(worldPos);
        float2 st1 = ddx(uv);
        float2 st2 = ddy(uv);

        float3 N = normalize(worldNormal);
        float3 T = normalize(Q1 * st2.y - Q2 * st1.y + constants::EPSILON);
        float3 B = normalize(cross(N, T));
        return float3x3(T, B, N);
    }
}