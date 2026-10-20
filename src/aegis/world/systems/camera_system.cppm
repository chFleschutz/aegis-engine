module;

#include "core/assert.h"

export module aegis.world.systems.camera_system;

import std;
import aegis.math;
import aegis.world.registry;
import aegis.world.entity;
import aegis.world.system;
import aegis.world.components;

export namespace aegis::world
{
class CameraSystem : public System
{
public:
    virtual void onUpdate(Registry& registry, float deltaSeconds) override
    {
        auto view = registry.view<GlobalTransform, Camera>();
        for (auto&& [entity, transform, camera] : view.each())
        {
            calcViewMatrix(camera, transform);
            calcPerspectiveProjection(camera);
        }
    }

private:
    void calcViewMatrix(Camera& camera, GlobalTransform& transform)
    {
        camera.viewMatrix = math::viewMatrix(transform.location, transform.forward());
        camera.inverseViewMatrix = math::inverseViewMatrix(transform.location, transform.forward());
    }

    void calcPerspectiveProjection(Camera& camera)
    {
        AGX_ASSERT(std::abs(camera.aspect - std::numeric_limits<float>::epsilon()) > 0.0f);

        // Projection matrix for right handed system with depth range [0, 1]
        // Note: Value [1][1] is negated because Vulkan uses a flipped y-axis
        const float tanHalfFovy = std::tan(camera.fov / 2.0f);
        glm::mat4 perspective{ 0.0f };
        perspective[0][0] = 1.0f / (camera.aspect * tanHalfFovy);
        perspective[1][1] = -1.0f / (tanHalfFovy);
        perspective[2][2] = camera.far / (camera.near - camera.far);
        perspective[2][3] = -1.0f;
        perspective[3][2] = -(camera.far * camera.near) / (camera.far - camera.near);
        camera.projectionMatrix = perspective;
    }
};
}