module;

#include <imgui.h>
#include <stb/stb_image.h>

module aegis.test;

import std;
import aegis.engine;
import aegis.render_graph;
import aegis.render_passes;
import aegis.rhi;
import aegis.world.registry;

namespace aegis::test
{
namespace
{
// DamagedHelmet under the Kloppenheim sky, a camera and three lights, in the sample project.
constexpr std::string_view sceneFile{ "scenes/tests/aegis_test.scene" };

// What the shell found, read once the engine (and the shell with it) is gone.
struct SceneResult
{
    std::vector<std::string> errors;
    rhi::Extent2D view{ viewExtent }; // --view: the view's size in the last frame
    std::uint32_t viewShown{ 0 };     // --view: frames whose UI showed the view (Presentation::uiTexture)
};

// The static entities of 'registry' a geometry pass draws: those with a mesh and a material.
auto drawableStatic(world::Registry& registry) -> std::uint32_t
{
    std::uint32_t count{ 0 };
    for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
    {
        if (renderer.mesh && renderer.material && !registry.has<DynamicTag>(world::Entity{ entity }))
            ++count;
    }
    return count;
}

// The static instances the instance pass wrote in the last frame; none on the CPU-driven path, which has no such pass.
auto writtenStatic(const render_graph::FrameGraph& frameGraph) -> std::optional<std::uint32_t>
{
    for (const auto* pass : frameGraph.passes())
    {
        if (const auto* instances = dynamic_cast<const render_graph::InstanceUpdatePass*>(pass))
            return instances->staticCount();
    }
    return std::nullopt;
}

// Destroys 'root' and its descendants.
void destroySubtree(world::Registry& registry, world::Entity root)
{
    std::vector<world::Entity> doomed{ root };
    for (std::size_t i = 0; i < doomed.size(); ++i)
    {
        for (auto child = registry.get<Children>(doomed[i]).first; child;
            child = registry.get<Siblings>(child).next)
            doomed.push_back(child);
    }
    for (const auto entity : doomed)
        registry.destroy(entity);
}

// The test's side of the app: edits the loaded scene at the safe point (--mirror, --mutate-scene), renders it
// through a second camera into a view of its own (--view), renders a second world for a while (--worlds), draws the
// stats window and checks the frame graph's timings. Failures go to the SceneResult; the run carries on.
class SceneShell final : public Shell
{
public:
    SceneShell(const Options& options, SceneResult& result) :
        m_options{ options },
        m_result{ result }
    {
    }

    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override
    {
        m_engine = &engine;
        return {};
    }

    void onFrame(FrameContext& frame) override
    {
        auto& registry = frame.world.registry();
        if (m_frame == 0)
            setUp(frame.world);
        frame.update(frame.world, world::UpdateMode::Play);

        if (m_options.mutateScene)
        {
            if (auto mutated = mutateScene(frame); !mutated)
                m_result.errors.push_back(std::format("Scene change failed: {}", mutated.error()));
        }
        if (m_options.worlds)
        {
            if (auto switched = switchWorlds(frame); !switched)
                m_result.errors.push_back(std::format("World switch failed: {}", switched.error()));
        }

        if (m_options.view)
        {
            // Headless runs resize the view halfway, so the graph's resolution-dependent textures and the views
            // passes derive from them are recreated under validation.
            if (m_options.frames != 0 && m_frame == m_options.frames / 2)
                m_result.view = resizedViewExtent;
            const auto extent = m_result.view;
            registry.get<Camera>(m_viewCamera).aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
        }

        buildUi(frame.frameGraph);
        if (m_options.view)
            showView(frame);
        if (m_options.frames != 0 && m_frame + 1 == m_options.frames)
            verifyTimings(frame.frameGraph);
        ++m_frame;
    }

    // The view's camera replaces the main one once the update has given it its matrices (from the second frame).
    [[nodiscard]] auto renderView() const -> std::optional<RenderView> override
    {
        if (!m_options.view)
            return std::nullopt;
        std::optional<RenderView::Camera> camera;
        if (m_frame > 1)
        {
            auto& registry = m_world->registry(); // Registry::get has no const overload
            const auto& view = registry.get<Camera>(m_viewCamera);
            camera = RenderView::Camera{
                .view = view.viewMatrix,
                .projection = view.projectionMatrix,
                .position = registry.get<GlobalTransform>(m_viewCamera).location,
            };
        }
        return RenderView{ .camera = camera, .extent = m_result.view };
    }

private:
    // The first frame: the edits the options ask for on the scene as the file left it.
    void setUp(world::World& world)
    {
        m_world = &world;
        auto& registry = world.registry();
        for (auto&& [entity, name] : registry.view<Name>().each())
        {
            if (name.name == "DamagedHelmet")
                m_helmet = world::Entity{ entity };
        }
        if (!m_helmet)
            m_result.errors.push_back(std::format("{} has no entity named DamagedHelmet", sceneFile));
        else if (m_options.mirror)
        {
            // A scale of -1 on x must not turn the helmet inside out.
            registry.get<Transform>(m_helmet).scale.x = -1.0f;
            registry.markStaticDirty(m_helmet);
        }

        if (m_options.view)
        {
            // Ahead and to the right of the main camera, turned towards the helmet; CameraSystem keeps its matrices.
            m_viewCamera = registry.create("View Camera", glm::vec3{ 2.2f, -2.2f, 0.3f }, glm::quat{ glm::radians(glm::vec3{ 0.0f, 0.0f, 45.0f }) });
            registry.add<Camera>(m_viewCamera);
            registry.markStaticDirty(m_viewCamera); // Once the world has begun, a new static entity needs it for its global transform
        }
    }

    // An instance of the model file 'path' (in the sample project) in the scene, as a root entity.
    auto instantiateModel(world::World& world, std::string_view path) -> std::expected<world::Entity, std::string>
    {
        const auto id = m_engine->assets().find(path);
        if (!id)
            return std::unexpected{ std::format("'{}' is not in the sample project", path) };
        return m_engine->assets().instantiateScene(world, m_engine->types(), *id, {});
    }

    // The UI pass's input: one window, so a screenshot shows ImGui drawn over the scene.
    void buildUi(const render_graph::FrameGraph& frameGraph) const
    {
        if (m_options.noOverlay)
            return;
        ImGui::SetNextWindowPos(ImVec2{ 10.0f, 10.0f });
        ImGui::Begin("aegis-test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Frame graph: %d passes", static_cast<int>(frameGraph.passes().size()));
        for (const auto& timing : frameGraph.timings())
            ImGui::Text("%-20s %.3f ms", timing.name.c_str(), timing.gpuMs);
        ImGui::End();
    }

    // --view: the view in a window of the UI, as an editor's viewport shows it, across the halfway resize.
    void showView(FrameContext& frame)
    {
        const auto final = frame.frameGraph.declarations().find(render_graph::names::finalImage);
        if (!frame.viewExtent || !final)
            return;
        const auto texture = frame.presentation.uiTexture(frame.frameGraph.pool().texture(*final).view);
        if (texture == 0)
            return;
        const auto extent = *frame.viewExtent;
        ImGui::SetNextWindowPos(ImVec2{ static_cast<float>(sceneExtent.x) / 2.0f, 10.0f });
        ImGui::Begin("View", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Image(ImTextureRef{ texture }, ImVec2{ static_cast<float>(extent.x) / 2.0f, static_cast<float>(extent.y) / 2.0f });
        ImGui::End();
        ++m_result.viewShown;
    }

    // The frame graph's own per-pass timestamps: one entry per pass, read back once complete.
    void verifyTimings(const render_graph::FrameGraph& frameGraph)
    {
        const auto timings = frameGraph.timings();
        if (timings.size() != frameGraph.passes().size())
            m_result.errors.push_back(std::format("Pass timings failed: {} timings for {} passes", timings.size(), frameGraph.passes().size()));
        for (const auto& [name, gpuMs, cpuMs] : timings)
            std::println("  {:<20} GPU {:7.4f} ms  CPU {:7.4f} ms", name, gpuMs, cpuMs);
    }

    // --mutate-scene: changes the scene the way an editor or gameplay code would, while frames are in
    // flight, and checks that the GPU-driven path follows. Called once per frame at the safe point.
    // Frame 30 loads a second model, 60 destroys the first one, 90 edits the second one's materials.
    // The frame after each change, the instance pass must have written as many static instances as
    // the scene holds. The last frame (--screenshot) shows the second model only, in red.
    auto mutateScene(FrameContext& frame) -> std::expected<void, std::string>
    {
        constexpr std::uint32_t loadFrame{ 30 };
        constexpr std::uint32_t destroyFrame{ 60 };
        constexpr std::uint32_t editFrame{ 90 };
        auto& registry = frame.world.registry();

        // The frame after a change: the instances written for it match the scene.
        const auto verifyWritten = [&](std::string_view what, bool grew) -> std::expected<void, std::string> {
            const auto expected = drawableStatic(registry);
            if (grew ? expected <= m_staticDrawn : expected >= m_staticDrawn)
                return std::unexpected{ std::format("{} did not change the scene's static instances ({} before, {} after)", what, m_staticDrawn, expected) };
            if (const auto written = writtenStatic(frame.frameGraph); written && *written != expected)
                return std::unexpected{ std::format("after {}: {} static instances written, the scene has {}", what, *written, expected) };
            std::println("Scene change verified: {} -> {} static instances after {}", m_staticDrawn, expected, what);
            m_staticDrawn = expected;
            return {};
        };

        if (m_frame == loadFrame)
        {
            m_staticDrawn = drawableStatic(registry);
            auto model = instantiateModel(frame.world, "models/sci_fi_helmet/SciFiHelmet.gltf");
            if (!model)
                return std::unexpected{ std::format("Loading the second model: {}", model.error()) };
            m_addedModel = *model;
            registry.get<Transform>(m_addedModel).location = glm::vec3{ 0.9f, 0.0f, 0.0f };
            registry.markStaticDirty(m_addedModel);
        }
        else if (m_frame == loadFrame + 1)
        {
            return verifyWritten("loading a model", true);
        }
        else if (m_frame == destroyFrame)
        {
            destroySubtree(registry, m_helmet);
        }
        else if (m_frame == destroyFrame + 1)
        {
            return verifyWritten("destroying a model", false);
        }
        else if (m_frame == editFrame)
        {
            std::uint32_t edited{ 0 };
            for (auto&& [entity, renderer] : registry.view<MeshRenderer>().each())
            {
                if (renderer.material && renderer.material->setParameter("albedo", glm::vec3{ 1.0f, 0.1f, 0.1f }))
                    ++edited;
            }
            if (edited == 0)
                return std::unexpected{ "no material to edit" };
        }
        return {};
    }

    // --worlds: a second world from the scene file, with the helmet left out so it draws fewer instances. It updates
    // once, in the first frame (it begins: its transforms and camera matrices are set), and then renders in the middle
    // third of the frames without updating. The main world updates in every frame and renders in the others, the
    // last one among them (--compare checks it against the plain scene's reference). The frame after each switch, the
    // instance pass must have written the static instances of the world it rendered. Called once per frame at the
    // safe point; without a frame count (a windowed run until closed) the second world never renders.
    auto switchWorlds(FrameContext& frame) -> std::expected<void, std::string>
    {
        const auto shownFrom = m_options.frames / 3;
        const auto shownUntil = 2 * m_options.frames / 3;
        if (m_frame == 0)
        {
            const auto scene = m_engine->assets().find(sceneFile);
            if (!scene || !m_helmet)
                return std::unexpected{ std::format("{} is not in the sample project, or has no helmet", sceneFile) };
            auto& second = frame.engine.createWorld();
            if (auto created = m_engine->assets().instantiateScene(second, m_engine->types(), *scene); !created)
            {
                frame.engine.destroyWorld(second);
                return std::unexpected{ std::format("Loading the second world: {}", created.error()) };
            }
            // The scene's entities keep their IDs in every world it is instantiated into.
            auto& registry = second.registry();
            destroySubtree(registry, registry.find(frame.world.registry().get<StableId>(m_helmet)));
            frame.update(second, world::UpdateMode::Play);
            m_second = &second;
        }
        if (!m_second || m_options.frames == 0)
            return {};

        if (m_frame >= shownFrom && m_frame < shownUntil)
            frame.render(*m_second);
        if (m_frame == shownFrom + 1 || m_frame == shownUntil + 1)
        {
            const bool second = m_frame == shownFrom + 1;
            const auto expected = drawableStatic(second ? m_second->registry() : frame.world.registry());
            const auto* name = second ? "second" : "main";
            if (const auto written = writtenStatic(frame.frameGraph); written && *written != expected)
                return std::unexpected{ std::format("{} static instances written, the {} world has {}", *written, name, expected) };
            std::println("World switch verified: the {} world rendered, {} static instances", name, expected);
        }
        if (m_frame + 1 == m_options.frames)
        {
            const auto secondUpdates = m_second->clock().frame();
            const auto mainUpdates = frame.world.clock().frame();
            if (secondUpdates != 1 || mainUpdates != m_frame)
                return std::unexpected{ std::format("in {} frames the second world updated {} times, the main world {}", m_frame, secondUpdates, mainUpdates) };
            std::println("Worlds verified: in {} frames the second world updated once, the main world in each", m_frame);
        }
        return {};
    }

    const Options& m_options;
    SceneResult& m_result;
    Engine* m_engine{ nullptr };
    world::World* m_world{ nullptr };
    std::uint32_t m_frame{ 0 };        // Frames begun so far
    world::Entity m_helmet;            // The holder of the model instance the scene file holds
    world::Entity m_addedModel;        // --mutate-scene: the holder of the model instance loaded later
    world::Entity m_viewCamera;        // --view: the camera the view renders through, not the main one
    std::uint32_t m_staticDrawn{ 0 };  // --mutate-scene: drawable static entities before the last change
    world::World* m_second{ nullptr }; // --worlds: the second world, from the same scene file
};

// Headless: the last frame, as the engine saved it, is drawn (not one flat color), has the size it should have and,
// with --compare, matches the reference.
auto verifyFrame(const Options& options, const std::filesystem::path& file, rhi::Extent2D extent) -> std::expected<void, std::string>
{
    int width{ 0 };
    int height{ 0 };
    int channels{ 0 };
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> image{
        stbi_load(file.string().c_str(), &width, &height, &channels, 4), &stbi_image_free
    };
    if (!image)
        return std::unexpected{ std::format("failed to read the frame '{}'", file.string()) };
    if (std::cmp_not_equal(width, extent.x) || std::cmp_not_equal(height, extent.y))
        return std::unexpected{ std::format("the frame is {}x{}, the {} {}x{}", width, height, options.view ? "view" : "target", extent.x, extent.y) };
    const auto pixels = std::as_bytes(std::span{ image.get(), std::size_t{ extent.x } * extent.y * 4 });

    if (!options.compare.empty())
    {
        if (auto matched = compareToReference(options, pixels, extent); !matched)
            return matched;
    }

    const auto texel = [&](std::uint32_t x, std::uint32_t y) {
        const auto offset = (std::size_t{ y } * extent.x + x) * 4;
        return std::array{ pixels[offset], pixels[offset + 1], pixels[offset + 2] };
    };
    if (texel(extent.x / 2, extent.y / 2) == texel(0, 0))
        return std::unexpected{ "the scene frame's centre and corner have the same color" };
    std::println("Offscreen scene frame verified: {}x{} {} read back, not uniform", extent.x, extent.y, options.view ? "view" : "target");
    return {};
}
}

auto runScene(const Options& options) -> int
{
    // Headless, the engine saves the last frame for the checks: to --screenshot, or to a file of its own.
    std::filesystem::path frameFile;
    if (options.headless)
    {
        frameFile = options.screenshot;
        if (frameFile.empty())
            frameFile = std::filesystem::temp_directory_path() / std::format("aegis-test-{:016x}.png", std::random_device{}() * 0x100000001ull);
    }

    auto engine = Engine::create({
        .title = "aegis-test",
        .width = sceneExtent.x,
        .height = sceneExtent.y,
        .headless = options.headless,
        .frames = options.frames,
        .screenshot = frameFile.string(),
        .gpuDriven = options.gpuDriven,
    });
    if (!engine)
    {
        std::println("Failed to create the engine\n{}", describe(engine.error()));
        return exitFailure;
    }
    if (options.gpuDriven && !(*engine)->useGpuDrivenRendering())
    {
        std::println("{}", skipMeshShaders);
        return skipExitCode;
    }

    SceneResult result;
    (*engine)->setShell(std::make_unique<SceneShell>(options, result));
    const auto scene = (*engine)->assets().find(sceneFile);
    if (!scene)
    {
        std::println("'{}' is not in the sample project", sceneFile);
        return exitFailure;
    }
    if (auto loaded = (*engine)->loadScene(*scene); !loaded)
    {
        std::println("{}", describe(loaded.error()));
        return exitFailure;
    }

    auto exitCode = (*engine)->run();
    engine->reset(); // The shell has finished with the result
    for (const auto& error : result.errors)
    {
        std::println("{}", error);
        exitCode = exitFailure;
    }
    // The graph follows the view from the first frame on, so the UI can show it in every frame.
    if (options.view && options.frames != 0)
    {
        if (result.viewShown != options.frames)
        {
            std::println("UI texture check failed: the UI showed the view in {} of {} frames", result.viewShown, options.frames);
            exitCode = exitFailure;
        }
        else
            std::println("UI texture verified: the view shown in {} frames, across a resize", result.viewShown);
    }

    if (options.headless && exitCode == exitSuccess)
    {
        if (auto verified = verifyFrame(options, frameFile, options.view ? result.view : sceneExtent); !verified)
        {
            std::println("Offscreen frame check failed: {}", verified.error());
            exitCode = exitFailure;
        }
    }
    if (options.headless && options.screenshot.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(frameFile, ignored);
    }
    return exitCode;
}
}
