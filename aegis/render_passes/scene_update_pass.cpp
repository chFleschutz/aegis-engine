module aegis.render_passes;
import std;
import aegis.math;

namespace aegis::render_graph
{
SceneUpdatePass::SceneUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view) :
    m_scene{ scene },
    m_view{ view },
    m_cameraData{ declarations.addBuffer(names::cameraData, Usage::HostWrite, { .size = sizeof(CameraData), .perFrame = true }) }
{
}

auto SceneUpdatePass::name() const -> std::string_view
{
    return "Scene Update";
}

auto SceneUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto camera = m_scene.current().camera(m_view);
    if (!camera)
        return;

    const auto data = detail::makeCameraData(*camera);
    pool.write(m_cameraData, std::as_bytes(std::span{ &data, 1 }));
}

namespace detail
{
auto makeCameraData(const FrameScene::Camera& camera) -> CameraData
{
    const auto viewProjection = camera.projection * camera.view;
    return {
        .view = camera.view,
        .projection = camera.projection,
        .viewProjection = viewProjection,
        .frustum = Frustum::extractFrom(viewProjection),
        .position = camera.position,
    };
}
}
}
