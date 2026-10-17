module aegis.render_graph;
import std;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
SceneUpdatePass::SceneUpdatePass(ResourcePool& pool) :
    m_cameraData{ pool.addBuffer("CameraData", Usage::HostWrite, { .size = sizeof(CameraData), .perFrame = true }) }
{
}

auto SceneUpdatePass::info() const -> Info
{
    return { .name = "Scene Update", .writes = { m_cameraData } };
}

auto SceneUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    auto& registry = frameInfo.scene.registry();
    const auto mainCamera = frameInfo.scene.mainCamera();
    if (!mainCamera || !registry.has<aegis::Camera, aegis::GlobalTransform>(mainCamera))
        return;

    // The camera system computes the projection on the next scene update, from this aspect.
    auto& camera = registry.get<aegis::Camera>(mainCamera);
    camera.aspect = static_cast<float>(frameInfo.extent.x) / static_cast<float>(frameInfo.extent.y);

    const auto viewProjection = camera.projectionMatrix * camera.viewMatrix;
    const CameraData data{
        .view = glm::rowMajor4(camera.viewMatrix),
        .projection = glm::rowMajor4(camera.projectionMatrix),
        .viewProjection = glm::rowMajor4(viewProjection),
        .frustum = Frustum::extractFrom(viewProjection),
        .position = registry.get<aegis::GlobalTransform>(mainCamera).location,
    };
    pool.write(m_cameraData, frameInfo.frameIndex, std::as_bytes(std::span{ &data, 1 }));
}
}
