module aegis.test;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.math;
import aegis.paths;
import aegis.render_graph;
import aegis.render_passes;
import aegis.renderer;
import aegis.rhi;

namespace aegis::test
{
namespace
{
// Copies "MotionVectors" into a host-readable buffer at the end of the frame.
class MotionReadbackPass : public render_graph::RenderPass
{
public:
    MotionReadbackPass(render_graph::ResourceDeclarations& declarations, rhi::BufferHandle readback) :
        m_motion{ declarations.addReference(render_graph::names::motionVectors, render_graph::Usage::TransferSrc) },
        m_readback{ readback }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Motion Readback"; }

    auto execute(render_graph::ResourcePool& pool, const render_graph::FrameInfo& frameInfo) -> void override
    {
        const auto extent = pool.extent(m_motion);
        const std::array regions{ rhi::BufferImageCopy{ .extent = { extent.x, extent.y, 1 } } };
        frameInfo.cmd.copyImageToBuffer(pool.texture(m_motion).image, pool.device().get(m_readback), regions);
    }

private:
    render_graph::ResourceId m_motion;
    rhi::BufferHandle m_readback;
};
}

// The G-buffer's motion vectors against their C++ twin (render_graph::detail::motionVector), on one path: the engine
// cube faces a jittering camera, moves sideways for a frame, then stays while the camera moves up, then the camera
// cuts. Every point of the cube's front face moves alike, so its texel at the view's centre stands for all of them;
// the sky around it keeps the clear value. The frame after the cube's move shows its GPU copy caught up
// (InstanceStore::endFrame): the camera's motion alone.
auto Harness::verifyMotionVectors(bool gpuDriven) -> std::expected<void, std::string>
{
    constexpr auto framesInFlight = renderer::Renderer::maxFramesInFlight;
    constexpr rhi::Extent2D extent{ 64, 64 };
    constexpr float tolerance{ 1e-3f }; // Half floats hold these values to about 3e-5
    const std::string_view path{ gpuDriven ? "GPU-driven" : "CPU-driven" };

    auto templates = loadEngineTemplates(paths::content(), paths::shaders());
    if (!templates)
        return std::unexpected{ std::format("Material templates: {}", describe(templates.error())) };
    auto defaults = assets::DefaultAssets::create(*m_device, framesInFlight, *templates);
    if (!defaults)
        return std::unexpected{ std::format("Default assets: {}", assets::describe(defaults.error())) };
    auto cube = assets::Loader::load({ .device = *m_device, .defaults = *defaults, .framesInFlight = framesInFlight },
        paths::content() / "cube.obj");
    if (!cube)
        return std::unexpected{ std::format("Loading the engine cube: {}", assets::describe(cube.error())) };
    if (auto flushed = flushAndWait(); !flushed)
        return flushed;

    const auto& primitive = cube->meshes.at(0).primitives.at(0);
    const auto material = primitive.material.shared();
    render_graph::InstanceStore store;
    const auto modelAt = [](float x) {
        auto model = math::Float4x4::identity();
        model[0, 3] = x;
        return render_graph::detail::toModelRows(model);
    };
    const auto id = store.add({ .model = modelAt(0.0f), .mesh = primitive.mesh.shared(), .material = material },
        material->variant(), true);
    if (!id)
        return std::unexpected{ std::format("Adding the cube: {}", render_graph::describe(id.error())) };

    auto readback = m_device->createBuffer({
        .name = "MotionVectorReadback",
        .size = std::size_t{ extent.x } * extent.y * 4, // RG16_SFLOAT
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "Failed to create the motion vector readback buffer" };

    std::expected<void, std::string> result;
    {
        render_graph::FrameSceneSource source;
        render_graph::PipelineLibrary pipelines;
        render_graph::SceneUpdatePass::Settings cameraSettings{ .jitter = true };
        render_graph::GraphBuilder builder;
        builder.add<render_graph::SceneUpdatePass>(source, 0u, &cameraSettings);
        if (gpuDriven)
        {
            builder.add<render_graph::InstanceUpdatePass>(source);
            builder.add<render_graph::CullingPass>(source, pipelines);
            builder.add<render_graph::DrawCompactionPass>(source, pipelines);
            builder.add<render_graph::GpuDrivenGeometryPass>(source, pipelines);
        }
        else
        {
            builder.add<render_graph::GeometryPass>(source, pipelines).addRenderSystem<render_graph::StaticMeshRenderSystem>();
        }
        builder.add<MotionReadbackPass>(*readback);
        auto created = render_graph::FrameGraph::create(*m_device, std::move(builder), extent);
        if (!created)
        {
            m_device->free(*readback);
            return std::unexpected{ std::format("Compiling the motion vector graph: {}", describe(created.error())) };
        }
        auto& graph = **created;

        // The camera looks along the world's forward axis (+y; z is up) at the cube's front face, y = -0.5.
        const auto projection = math::perspective(math::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const auto cameraAt = [&](math::Float3 position, bool cut) {
            return render_graph::FrameScene::Camera{
                .view = math::viewMatrix(position, math::world::forward<float>),
                .projection = projection,
                .position = position,
                .cut = cut,
            };
        };
        // The clip position of world point 'point' through 'camera', unjittered.
        const auto clip = [](const render_graph::FrameScene::Camera& camera, math::Float3 point) {
            return camera.projection * camera.view * math::Float4{ point.x, point.y, point.z, 1.0f };
        };
        // Within the tolerance; never for a NaN, which compares false either way.
        const auto near = [&](math::Float2 actual, math::Float2 expected) {
            return std::isfinite(actual.x) && std::isfinite(actual.y) && std::abs(actual.x - expected.x) <= tolerance &&
                   std::abs(actual.y - expected.y) <= tolerance;
        };

        std::uint32_t frame{ 0 };
        // Renders a frame through 'camera' and returns its motion vector at texel (x, y).
        const auto render = [&](const render_graph::FrameScene::Camera& camera, std::uint32_t x,
                                std::uint32_t y) -> std::expected<math::Float2, std::string> {
            const render_graph::FrameScene scene{ .instances = store, .views = { { .name = "Main", .camera = camera } } };
            const auto bound = source.bind(scene);
            if (auto prepared = graph.prepare(); !prepared)
                return std::unexpected{ std::format("Preparing frame {}: {}", frame, describe(prepared.error())) };
            auto ran = submitAndWait("MotionVectorFrame", [&](const auto& cmd) {
                // The graph declares no frame target: nothing to bind
                graph.execute(cmd, frame % framesInFlight, {});
            });
            if (!ran)
                return std::unexpected{ ran.error() };
            store.endFrame();
            ++frame;

            std::vector<std::uint16_t> halves(std::size_t{ extent.x } * extent.y * 2);
            m_device->get(*readback).read(reinterpret_cast<std::byte*>(halves.data()), halves.size() * 2);
            const auto texel = (std::size_t{ y } * extent.x + x) * 2;
            return math::Float2{ halfToFloat(halves[texel]), halfToFloat(halves[texel + 1]) };
        };
        // One frame through 'camera': the cube's motion vector at the view's centre is 'expected'.
        const auto expectCentre = [&](std::string_view when, const render_graph::FrameScene::Camera& camera,
                                      math::Float2 expected) -> std::expected<void, std::string> {
            const auto centre = render(camera, extent.x / 2, extent.y / 2);
            if (!centre)
                return std::unexpected{ centre.error() };
            if (!near(*centre, expected))
            {
                return std::unexpected{ std::format("{}: the cube's motion vector is ({}, {}), want ({}, {})", when,
                    centre->x, centre->y, expected.x, expected.y) };
            }
            return {};
        };
        // Two frames through 'camera', which see no motion: the cube at the centre, then the sky in the corner.
        const auto expectStill = [&](std::string_view when, const render_graph::FrameScene::Camera& camera)
            -> std::expected<void, std::string> {
            if (auto centre = expectCentre(when, camera, {}); !centre)
                return centre;
            const auto sky = render(camera, 0, 0);
            if (!sky)
                return std::unexpected{ sky.error() };
            if (*sky != math::Float2{})
                return std::unexpected{ std::format("{}: the sky's motion vector is ({}, {}), want none", when, sky->x, sky->y) };
            return {};
        };

        result = [&]() -> std::expected<void, std::string> {
            const auto still = cameraAt({ 0.0f, -3.0f, 0.0f }, false);
            if (auto first = expectStill("The first frame", cameraAt({ 0.0f, -3.0f, 0.0f }, true)); !first)
                return first;

            // The cube moves 0.1 right: the point of its front face now at the centre was 0.1 further left.
            if (auto updated = store.update(*id, { .model = modelAt(0.1f), .mesh = primitive.mesh.shared(), .material = material },
                    material->variant());
                !updated)
                return std::unexpected{ std::format("Moving the cube: {}", render_graph::describe(updated.error())) };
            const math::Float3 face{ 0.0f, -0.5f, 0.0f };
            const auto cubeExpected = render_graph::detail::motionVector(clip(still, face), clip(still, face - math::Float3{ 0.1f, 0.0f, 0.0f }));
            if (!(std::abs(cubeExpected.x) > 10.0f * tolerance))
                return std::unexpected{ std::format("The cube's expected motion ({}, {}) is too small to check", cubeExpected.x, cubeExpected.y) };
            if (auto moved = expectCentre("The cube moving", still, cubeExpected); !moved)
                return moved;

            // The camera moves 0.2 up and the cube stays: the camera's motion alone, so the cube's GPU copies caught up
            // (each frame in flight's, over the next frames).
            const auto raised = cameraAt({ 0.0f, -3.0f, 0.2f }, false);
            const math::Float3 raisedFace{ 0.0f, -0.5f, 0.2f };
            const auto cameraExpected = render_graph::detail::motionVector(clip(raised, raisedFace), clip(still, raisedFace));
            if (!(std::abs(cameraExpected.y) > 10.0f * tolerance))
                return std::unexpected{ std::format("The camera's expected motion ({}, {}) is too small to check", cameraExpected.x, cameraExpected.y) };
            if (auto moved = expectCentre("The camera moving", raised, cameraExpected); !moved)
                return moved;
            for (std::uint32_t i = 0; i < framesInFlight; ++i)
            {
                if (auto stopped = expectStill("The camera stopped", raised); !stopped)
                    return stopped;
            }

            // A cut elsewhere: no motion from the frame before it.
            return expectStill("A camera cut", cameraAt({ 0.0f, -2.0f, -0.2f }, true));
        }();
        m_device->waitIdle();
    }
    m_device->free(*readback);
    if (!result)
        return std::unexpected{ std::format("Motion vectors ({}): {}", path, result.error()) };

    std::println("Motion vectors verified ({}): a moving cube, then a moving camera with jitter, then a cut", path);
    return {};
}
}
