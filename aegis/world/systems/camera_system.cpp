module;

#include <cassert>

module aegis.world;

import std;

namespace aegis::world
{
void CameraSystem::onUpdate(Registry& registry, float deltaSeconds)
{
    auto view = registry.view<GlobalTransform, Camera, CameraMatrices>();
    for (auto&& [entity, transform, camera, matrices] : view.each())
    {
        calcViewMatrix(matrices, transform);
        calcPerspectiveProjection(matrices, camera);
    }
}

void CameraSystem::calcViewMatrix(CameraMatrices& matrices, GlobalTransform& transform)
{
    matrices.view = math::viewMatrix(transform.location(), transform.forward());
    matrices.inverseView = math::inverseViewMatrix(transform.location(), transform.forward());
}

void CameraSystem::calcPerspectiveProjection(CameraMatrices& matrices, const Camera& camera)
{
    assert(std::abs(matrices.aspect - std::numeric_limits<float>::epsilon()) > 0.0f);

    matrices.projection = math::perspective(camera.fov, matrices.aspect, camera.near, camera.far);
}
}
