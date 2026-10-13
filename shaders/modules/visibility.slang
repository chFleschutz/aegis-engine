module visibility;

import modules.common;

namespace visibility
{
    public func frustumVisible(common::BoundingSphere sphere, common::Frustum frustum) -> bool
    {
        for (int i = 0; i < 6; i++)
        {
            let plane = frustum.planes[i];
            let distance = dot(plane.xyz, sphere.center) + plane.w;
            if (distance < -sphere.radius)
                return false;
        }
        return true;
    }

    public func coneVisible(common::BoundingSphere worldBounds, float3 worldConeAxis, common::NormalCone cone, float3 cameraPos) -> bool
    {
        if (cone.cutoff_s8 == 127)
            return true; // Degenerate cone -> assume always visible

        float3 cameraToCenter = worldBounds.center - cameraPos;
        float centerDist = length(cameraToCenter);
        return dot(cameraToCenter, worldConeAxis) < cone.cutoff * centerDist + worldBounds.radius;
    }
}
