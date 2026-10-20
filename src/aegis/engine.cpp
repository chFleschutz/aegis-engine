module;

#include "core/assert.h"

#include <cstdlib>

#include <imgui.h>
#include <imgui_impl_glfw.h>

module aegis.engine;

import std;

namespace aegis
{
namespace
{
// Setup failures log their cause and exit; the engine can't carry on without a device.
[[noreturn]] void fatalSetup(const std::string& message)
{
    log::fatal("{}", message);
    std::exit(EXIT_FAILURE);
}

template<typename T, typename E>
auto check(std::expected<T, E> result, const std::string& what) -> T
{
    if (!result)
    {
        // Module errors with a describe() print their code and rhi cause too.
        if constexpr (requires { describe(result.error()); })
            fatalSetup(std::format("Failed to {}: {}", what, describe(result.error())));
        else
            fatalSetup(std::format("Failed to {}: {}", what, result.error().message));
    }
    return std::move(*result);
}
}

Engine::Engine()
{
    AGX_ASSERT_X(!s_instance, "Only one instance of Engine is allowed");
    s_instance = this;

    createRenderer();
    loadDefaultAssets();
    createFrameGraph();
    m_layerStack.push<editor::EditorLayer>(m_input, *m_renderer, *m_frameGraph, m_drawBatches, m_scene);

    log::info("Engine Initialized!");
    log::info("Using {} Driven Rendering", useGpuDrivenRendering() ? "GPU" : "CPU");
    log::info("\n\n\n"
              "\t\t\t\t      ###   #######   ######   ####   ###### \n"
              "\t\t\t\t     ## ##  ##       ##    ##   ##   ##    ##\n"
              "\t\t\t\t    ##  ##  ##       ##         ##   ##      \n"
              "\t\t\t\t   ##   ##  ######   ##         ##    ###### \n"
              "\t\t\t\t  ########  ##       ##  ####   ##         ##\n"
              "\t\t\t\t ##     ##  ##       ##    ##   ##   ##    ##\n"
              "\t\t\t\t##      ##  #######   ######   ####   ###### \n"
              "\n\n");
}

Engine::~Engine()
{
    // The GPU may still use what the members below free; ImGui's backends go before its context.
    m_device->waitIdle();
    m_frameGraph.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    s_instance = nullptr;
}

auto Engine::instance() -> Engine&
{
    AGX_ASSERT_X(s_instance, "Engine instance not created yet");
    return *s_instance;
}

auto Engine::window() -> aegis::platform::Window&
{
    return instance().m_window;
}
auto Engine::input() -> aegis::platform::Input&
{
    return instance().m_input;
}
auto Engine::device() -> aegis::rhi::Device&
{
    return *instance().m_device;
}
auto Engine::renderer() -> aegis::renderer::Renderer&
{
    return *instance().m_renderer;
}
auto Engine::frameGraph() -> aegis::render_graph::FrameGraph&
{
    return *instance().m_frameGraph;
}
auto Engine::drawBatches() -> aegis::render_graph::DrawBatchRegistry&
{
    return instance().m_drawBatches;
}
auto Engine::ui() -> ui::UI&
{
    return instance().m_ui;
}
auto Engine::scene() -> scene::Scene&
{
    return instance().m_scene;
}
auto Engine::defaults() -> const aegis::assets::DefaultAssets&
{
    return *instance().m_defaults;
}

auto Engine::useGpuDrivenRendering() const -> bool
{
    return ENABLE_GPU_DRIVEN_RENDERING && m_device->capabilities().meshShaders;
}

auto Engine::loadModel(const std::filesystem::path& path) -> scene::Entity
{
    auto& engine = instance();
    auto root = aegis::assets::Loader::load(
        {
            .device = *engine.m_device,
            .scene = engine.m_scene.registry(),
            .pbrTemplate = engine.m_defaults->pbrTemplate,
            .defaultMaterial = engine.m_defaults->pbrMaterial,
            .framesInFlight = FRAMES_IN_FLIGHT,
        },
        path);
    return check(std::move(root), std::format("load model '{}'", path.string()));
}

auto Engine::loadEnvironment(const std::filesystem::path& hdr) -> aegis::assets::Environment
{
    return check(instance().m_ibl->loadEnvironment(hdr), std::format("load environment '{}'", hdr.string()));
}

auto Engine::loadTexture(const std::filesystem::path& path, aegis::rhi::Format format) -> std::shared_ptr<aegis::assets::Texture>
{
    return check(aegis::assets::Texture::loadFromFile(*instance().m_device, path, format),
        std::format("load texture '{}'", path.string()));
}

auto Engine::createMaterial(std::shared_ptr<const aegis::assets::MaterialTemplate> materialTemplate)
    -> std::shared_ptr<aegis::assets::MaterialInstance>
{
    auto& engine = instance();
    if (!materialTemplate)
        materialTemplate = engine.m_defaults->pbrTemplate;
    return check(aegis::assets::MaterialInstance::create(*engine.m_device, std::move(materialTemplate), FRAMES_IN_FLIGHT),
        "create a material");
}

void Engine::run()
{
    auto lastFrameBegin = std::chrono::steady_clock::now();
    while (!m_window.shouldClose())
    {
        ScopeProfiler frameTime("Frame Time");

        auto currentFrameBegin = std::chrono::steady_clock::now();
        float frameTimeSec = std::chrono::duration<float, std::chrono::seconds::period>(currentFrameBegin - lastFrameBegin).count();
        lastFrameBegin = currentFrameBegin;
        // After a long frame (a breakpoint, a load), don't hand scripts and systems the whole stall
        frameTimeSec = std::min(frameTimeSec, MAX_FRAME_DELTA_SEC);

        m_window.update();
        if (m_window.isMinimized())
        {
            m_window.waitEvents();
            lastFrameBegin = std::chrono::steady_clock::now(); // Not part of the next frame's delta
            continue;
        }
        if (m_window.wasResized() || m_renderer->needsResize())
        {
            if (!resize())
                break;
        }

        // Update: scripts write the transforms, the systems derive from them (camera matrices
        // included), then the editor reads the result.
        // ImGui's flags are from the previous frame's UI; they gate the game input.
        const auto& io = ImGui::GetIO();
        m_input.setCaptured(io.WantCaptureKeyboard, io.WantCaptureMouse);
        updateCameraAspect();
        m_scriptManager.update(frameTimeSec);
        m_scene.update(frameTimeSec);
        m_layerStack.update(frameTimeSec);

        // Rendering: uploads queued this frame (or while loading) go first, on the same queue.
        if (auto flushed = m_device->flushUploads(); !flushed)
        {
            log::fatal("Failed to flush uploads: {}", rhi::describe(flushed.error()));
            break;
        }
        auto rendered = m_renderer->renderFrame([this](const aegis::renderer::Renderer::FrameInfo& frame) {
            m_frameGraph->execute({
                .cmd = frame.cmd,
                .scene = m_scene,
                .drawBatches = m_drawBatches,
                .frameIndex = frame.frameIndex,
                .extent = frame.extent,
                .target = frame.target,
            });
        });
        if (!rendered)
        {
            log::fatal("Failed to render a frame: {}", aegis::renderer::describe(rendered.error()));
            break;
        }

        applyFrameBrake(currentFrameBegin);
    }

    m_device->waitIdle();
}

void Engine::createRenderer()
{
    auto context = aegis::rhi::Context::create({ .appName = "Aegis", .window = &m_window });
    if (!context)
        fatalSetup(std::format("Failed to create the Vulkan context ({})", aegis::rhi::describe(context.error())));
    m_context = std::move(*context);

    auto device = m_context->createDevice({});
    if (!device)
        fatalSetup(std::format("Failed to create the Vulkan device ({})", aegis::rhi::describe(device.error())));
    m_device = std::move(*device);

    auto renderer = aegis::renderer::Renderer::create({
        .context = *m_context,
        .device = *m_device,
        .window = &m_window,
    });
    if (!renderer)
        fatalSetup(std::format("Failed to create the renderer: {}", aegis::renderer::describe(renderer.error())));
    m_renderer = std::move(*renderer);
}

void Engine::loadDefaultAssets()
{
    m_defaults = check(aegis::assets::DefaultAssets::create(*m_device, FRAMES_IN_FLIGHT), "create the default assets");
    auto ibl = aegis::render_graph::Ibl::create(*m_device);
    if (!ibl)
        fatalSetup(std::format("Failed to create the IBL pipelines: {}", describe(ibl.error())));
    m_ibl.emplace(std::move(*ibl));
    m_brdfLut = check(m_ibl->brdfLut(), "compute the BRDF lookup table");
}

void Engine::createFrameGraph()
{
    m_drawBatches.connect(m_scene.registry());

    ImGui_ImplGlfw_InitForVulkan(m_window.glfwWindow(), true);

    m_frameGraph = std::make_unique<aegis::render_graph::FrameGraph>(*m_device, *m_renderer);
    aegis::render_graph::addDefaultPasses(*m_frameGraph,
        {
            .gpuDriven = useGpuDrivenRendering(),
            .drawBatches = m_drawBatches,
            .buildUi = [this] {
                ImGui_ImplGlfw_NewFrame();
                m_ui.render();
            },
            .context = m_context.get(),
        });
    if (auto compiled = m_frameGraph->compile(); !compiled)
    {
        log::fatal("Failed to compile the frame graph: {}", describe(compiled.error()));
        AGX_ASSERT_X(false, "Failed to compile the frame graph");
    }
}

auto Engine::defaultEnvironment() const -> aegis::assets::Environment
{
    return aegis::assets::Environment{
        .skybox = m_defaults->blackCube,
        .irradiance = m_defaults->blackCube,
        .prefiltered = m_defaults->blackCube,
        .brdfLUT = m_brdfLut,
    };
}

bool Engine::resize()
{
    if (auto resized = m_renderer->resize(aegis::rhi::Extent2D{ m_window.extent() }); !resized)
    {
        log::fatal("Failed to resize the renderer: {}", aegis::renderer::describe(resized.error()));
        return false;
    }
    m_window.resetResized();
    if (auto resized = m_frameGraph->resized(); !resized)
        log::fatal("Failed to resize the frame graph: {}", describe(resized.error()));
    return true;
}

void Engine::updateCameraAspect()
{
    const auto [width, height] = m_window.extent();
    auto& registry = m_scene.registry();
    const auto mainCamera = m_scene.mainCamera();
    if (height == 0 || !mainCamera || !registry.has<Camera>(mainCamera))
        return;
    registry.get<Camera>(mainCamera).aspect = static_cast<float>(width) / static_cast<float>(height);
}

void Engine::applyFrameBrake(std::chrono::steady_clock::time_point frameBegin)
{
    using namespace std::chrono;

    ScopeProfiler frameBrake("Wait for FPS limit");

    if constexpr (!core::ENABLE_FPS_LIMIT)
        return;

    // Note: This has to be a busy wait since sleep_for is not accurate enough (especially on Windows)
    while (duration<double, std::milli>(steady_clock::now() - frameBegin).count() < core::TARGET_FRAME_TIME)
    {
        // busy wait
    }
}
}
