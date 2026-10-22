module;

#include <imgui.h>

module aegis.test;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.paths;
import aegis.reflect;
import aegis.render_bridge;
import aegis.renderer;
import aegis.render_graph;
import aegis.rhi;
import aegis.math;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.systems.camera_system;
import aegis.world.systems.transform_system;
import aegis.world.types;

namespace aegis
{
// The render graph's input: DamagedHelmet under the Kloppenheim sky, a camera and two lights,
// and the CPU-driven frame graph to draw it.
auto Engine::createScene() -> std::expected<void, std::string>
{
    constexpr auto framesInFlight = renderer::Renderer::maxFramesInFlight;
    auto library = AssetLibrary::create(*m_device, framesInFlight);
    if (!library)
        return std::unexpected{ std::format("Asset library: {}", library.error()) };
    m_assets = std::move(*library);
    if (auto registered = world::registerComponents(m_types); !registered)
        return std::unexpected{ std::format("Component types: {}", reflect::describe(registered.error())) };

    m_scene = std::make_unique<aegis::world::World>();
    m_scene->addSystem<aegis::world::TransformSystem>();
    m_scene->addSystem<aegis::world::CameraSystem>();
    m_instances = std::make_unique<render_graph::InstanceStore>();
    m_instanceBridge = std::make_unique<InstanceBridge>(*m_instances);
    m_instanceBridge->connect(m_scene->registry());

    auto& registry = m_scene->registry();
    const auto camera = registry.create("Camera", glm::vec3{ 0.0f, -3.0f, 0.0f });
    registry.add<aegis::Camera>(camera);
    m_scene->setMainCamera(camera);

    const auto ambient = registry.create("Ambient Light");
    registry.add<aegis::AmbientLight>(ambient).intensity = 1.0f;

    const auto sun = registry.create("Directional Light");
    registry.add<aegis::DirectionalLight>(sun);
    registry.get<aegis::Transform>(sun).rotation = glm::quat{ glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f }) };

    // In view, off to the side of the helmet: drawn as a billboard by the transparent pass.
    const auto lamp = registry.create("Point Light", glm::vec3{ 1.2f, -1.0f, 0.6f });
    registry.add<aegis::PointLight>(lamp).color = glm::vec3{ 1.0f, 0.4f, 0.1f };

    auto ibl = render_graph::Ibl::create(*m_device);
    if (!ibl)
        return std::unexpected{ std::format("IBL: {}", describe(ibl.error())) };
    auto environment = ibl->loadEnvironment(paths::assets() / "Environments/KloppenheimSky.hdr");
    if (!environment)
        return std::unexpected{ std::format("Environment: {}", describe(environment.error())) };
    const auto sky = registry.create("Sky");
    registry.add<aegis::Sky>(sky, assets::AssetRef<assets::Environment>::ready(assets::AssetId::generate(), std::move(*environment)));

    auto helmet = instantiateModel("DamagedHelmet/DamagedHelmet.gltf");
    if (!helmet)
        return std::unexpected{ std::format("Loading the helmet: {}", helmet.error()) };
    m_helmet = *helmet;
    if (m_options.mirror)
        registry.get<aegis::Transform>(m_helmet).scale.x = -1.0f;

    // No platform backend: the UI is drawn, not interacted with.
    m_imgui = ImGuiContextHandle{ ImGui::CreateContext() };
    ImGui::GetIO().IniFilename = nullptr;

    m_frameGraph = std::make_unique<render_graph::FrameGraph>(*m_device, m_renderer->extent());
    render_graph::addDefaultPasses(*m_frameGraph,
        {
            .gpuDriven = m_options.gpuDriven,
            .ui = true,
            .context = m_context.get(),
        });
    if (auto compiled = m_frameGraph->compile(); !compiled)
        return std::unexpected{ std::format("Frame graph: {}", describe(compiled.error())) };

    m_scene->begin();

    std::print("Frame graph compiled:");
    for (const auto* pass : m_frameGraph->passes())
        std::print(" {} |", pass->name());
    std::println("");
    return {};
}

// An instance of the model file 'path' (in the assets project) in the scene, as a root entity.
auto Engine::instantiateModel(std::string_view path) -> std::expected<aegis::world::Entity, std::string>
{
    const auto id = m_assets->find(path);
    if (!id)
        return std::unexpected{ std::format("'{}' is not in the assets project", path) };
    return m_assets->instantiateScene(*m_scene, m_types, *id, {});
}

// The UI pass's input: one window, so a screenshot shows ImGui drawn over the scene. Ends the frame
// (ImGui::Render), so the frame graph's UI pass records it.
auto Engine::buildUi(const renderer::Renderer& renderer, const render_graph::FrameGraph& frameGraph, bool overlay) -> void
{
    auto& io = ImGui::GetIO();
    const auto extent = renderer.extent();
    io.DisplaySize = ImVec2{ static_cast<float>(extent.x), static_cast<float>(extent.y) };
    ImGui::NewFrame();
    if (overlay)
    {
        ImGui::SetNextWindowPos(ImVec2{ 10.0f, 10.0f });
        ImGui::Begin("aegis-test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Frame graph: %d passes", static_cast<int>(frameGraph.passes().size()));
        for (const auto& timing : frameGraph.timings())
            ImGui::Text("%-20s %.3f ms", timing.name.c_str(), timing.gpuMs);
        ImGui::End();
    }
    ImGui::Render();
}

// --mutate-scene: changes the scene the way an editor or gameplay code would, while frames are in
// flight, and checks that the GPU-driven path follows. Called once per frame before it is recorded.
// Frame 30 loads a second model, 60 destroys the first one, 90 edits the second one's materials.
// The frame after each change, the instance pass must have written as many static instances as
// the scene holds. The last frame (--screenshot) shows the second model only, in red.
auto Engine::mutateScene(std::uint32_t frame) -> std::expected<void, std::string>
{
    constexpr std::uint32_t loadFrame{ 30 };
    constexpr std::uint32_t destroyFrame{ 60 };
    constexpr std::uint32_t editFrame{ 90 };
    auto& registry = m_scene->registry();

    const auto drawableStatic = [&] {
        std::uint32_t count{ 0 };
        for (auto&& [entity, renderer] : registry.view<aegis::MeshRenderer>().each())
        {
            if (renderer.mesh && renderer.material && !registry.has<aegis::DynamicTag>(aegis::world::Entity{ entity }))
                ++count;
        }
        return count;
    };
    const auto writtenStatic = [&]() -> std::optional<std::uint32_t> {
        for (auto* pass : m_frameGraph->passes())
        {
            if (const auto* instances = dynamic_cast<render_graph::InstanceUpdatePass*>(pass))
                return instances->staticCount();
        }
        return std::nullopt; // The CPU-driven path has no instance pass
    };
    // The frame after a change: the instances written for it match the scene.
    const auto verifyWritten = [&](std::string_view what, bool grew) -> std::expected<void, std::string> {
        const auto expected = drawableStatic();
        if (grew ? expected <= m_staticDrawn : expected >= m_staticDrawn)
            return std::unexpected{ std::format("{} did not change the scene's static instances ({} before, {} after)", what, m_staticDrawn, expected) };
        if (const auto written = writtenStatic(); written && *written != expected)
            return std::unexpected{ std::format("after {}: {} static instances written, the scene has {}", what, *written, expected) };
        std::println("Scene change verified: {} -> {} static instances after {}", m_staticDrawn, expected, what);
        m_staticDrawn = expected;
        return {};
    };

    if (frame == loadFrame)
    {
        m_staticDrawn = drawableStatic();
        auto model = instantiateModel("SciFiHelmet/SciFiHelmet.gltf");
        if (!model)
            return std::unexpected{ std::format("Loading the second model: {}", model.error()) };
        m_addedModel = *model;
        registry.get<aegis::Transform>(m_addedModel).location = glm::vec3{ 0.9f, 0.0f, 0.0f };
        registry.markStaticDirty(m_addedModel);
    }
    else if (frame == loadFrame + 1)
    {
        return verifyWritten("loading a model", true);
    }
    else if (frame == destroyFrame)
    {
        std::vector<aegis::world::Entity> doomed{ m_helmet };
        for (std::size_t i = 0; i < doomed.size(); ++i)
        {
            for (auto child = registry.get<aegis::Children>(doomed[i]).first; child;
                child = registry.get<aegis::Siblings>(child).next)
                doomed.push_back(child);
        }
        for (const auto entity : doomed)
            registry.destroy(entity);
    }
    else if (frame == destroyFrame + 1)
    {
        return verifyWritten("destroying a model", false);
    }
    else if (frame == editFrame)
    {
        std::uint32_t edited{ 0 };
        for (auto&& [entity, renderer] : registry.view<aegis::MeshRenderer>().each())
        {
            if (renderer.material && renderer.material->setParameter("albedo", glm::vec3{ 1.0f, 0.1f, 0.1f }))
                ++edited;
        }
        if (edited == 0)
            return std::unexpected{ "no material to edit" };
    }
    return {};
}
}
