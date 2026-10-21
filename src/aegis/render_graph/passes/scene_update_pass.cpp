module aegis.render_graph;
import std;
import aegis.math;

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
    const auto& camera = frameInfo.frameScene.camera;
    if (!camera)
        return;

    const auto data = detail::makeCameraData(*camera);
    pool.write(m_cameraData, frameInfo.frameIndex, std::as_bytes(std::span{ &data, 1 }));
}

namespace detail
{
auto makeCameraData(const FrameScene::Camera& camera) -> CameraData
{
    const auto viewProjection = camera.projection * camera.view;
    return {
        .view = glm::rowMajor4(camera.view),
        .projection = glm::rowMajor4(camera.projection),
        .viewProjection = glm::rowMajor4(viewProjection),
        .frustum = Frustum::extractFrom(viewProjection),
        .position = camera.position,
    };
}
}
}
