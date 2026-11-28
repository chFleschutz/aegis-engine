module aegis.render_passes;
import std;
import aegis.math;

namespace aegis::render_graph
{
SceneUpdatePass::SceneUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene, std::uint32_t view,
    const TemporalAaPass::Settings* temporalAa) :
    m_scene{ scene },
    m_view{ view },
    m_temporalAa{ temporalAa },
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
    {
        m_previousViewProjection.reset();
        return;
    }

    const auto jitter = m_temporalAa && m_temporalAa->enabled
                            ? detail::jitterOffset(detail::haltonJitter(m_frame), frameInfo.extent)
                            : math::Float2{};
    const auto data = detail::makeCameraData(*camera, jitter, camera->cut ? std::nullopt : m_previousViewProjection);
    pool.write(m_cameraData, std::as_bytes(std::span{ &data, 1 }));
    m_previousViewProjection = data.viewProjection;
    m_frame = (m_frame + 1) % jitterSequenceLength;
}

namespace detail
{
namespace
{
// The radical inverse of 'index' in 'base': its digits mirrored behind the point, in [0, 1).
auto radicalInverse(std::uint32_t index, std::uint32_t base) -> float
{
    float result = 0.0f;
    float scale = 1.0f / static_cast<float>(base);
    for (; index > 0; index /= base, scale /= static_cast<float>(base))
        result += static_cast<float>(index % base) * scale;
    return result;
}
}

auto haltonJitter(std::uint32_t frame) -> math::Float2
{
    const auto index = frame % jitterSequenceLength + 1;
    return { radicalInverse(index, 2) - 0.5f, radicalInverse(index, 3) - 0.5f };
}

auto jitterOffset(math::Float2 pixels, rhi::Extent2D extent) -> math::Float2
{
    if (extent.x == 0 || extent.y == 0)
        return {};
    return { 2.0f * pixels.x / static_cast<float>(extent.x), 2.0f * pixels.y / static_cast<float>(extent.y) };
}

auto jitterProjection(const math::Float4x4& projection, math::Float2 offset) -> math::Float4x4
{
    // Clip x and y move by offset * w, which the perspective divide turns into 'offset': the shift's row adds w's row.
    auto shift = math::Float4x4::identity();
    shift[0, 3] = offset.x;
    shift[1, 3] = offset.y;
    return shift * projection;
}

auto makeCameraData(const FrameScene::Camera& camera, math::Float2 jitter, const std::optional<math::Float4x4>& previousViewProjection)
    -> CameraData
{
    const auto viewProjection = camera.projection * camera.view;
    const auto jitteredProjection = jitterProjection(camera.projection, jitter);
    return {
        .view = camera.view,
        .projection = camera.projection,
        .viewProjection = viewProjection,
        .jitteredProjection = jitteredProjection,
        .jitteredViewProjection = jitteredProjection * camera.view,
        .previousViewProjection = previousViewProjection.value_or(viewProjection),
        .frustum = Frustum::extractFrom(viewProjection),
        .position = camera.position,
        .jitter = jitter,
    };
}
}
}
