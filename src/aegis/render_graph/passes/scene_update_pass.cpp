module aegis.render_graph;
import std;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
SceneUpdatePass::SceneUpdatePass(ResourceDeclarations& declarations) :
    m_cameraData{ declarations.addBuffer(names::cameraData, Usage::HostWrite, { .size = sizeof(CameraData), .perFrame = true }) }
{
}

auto SceneUpdatePass::name() const -> std::string_view
{
    return "Scene Update";
}

auto SceneUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    auto& registry = frameInfo.scene.registry();
    const auto mainCamera = frameInfo.scene.mainCamera();
    if (!mainCamera || !registry.has<aegis::Camera, aegis::GlobalTransform>(mainCamera))
        return;

    const auto data = detail::makeCameraData(
        registry.get<aegis::Camera>(mainCamera), registry.get<aegis::GlobalTransform>(mainCamera));
    pool.write(m_cameraData, frameInfo.frameIndex, std::as_bytes(std::span{ &data, 1 }));
}

namespace detail
{
auto makeCameraData(const aegis::Camera& camera, const aegis::GlobalTransform& transform) -> CameraData
{
    const auto viewProjection = camera.projectionMatrix * camera.viewMatrix;
    return {
        .view = glm::rowMajor4(camera.viewMatrix),
        .projection = glm::rowMajor4(camera.projectionMatrix),
        .viewProjection = glm::rowMajor4(viewProjection),
        .frustum = Frustum::extractFrom(viewProjection),
        .position = transform.location,
    };
}
}
}
